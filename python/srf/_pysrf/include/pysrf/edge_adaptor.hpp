#pragma once

#pragma once

#include <pysrf/types.hpp>  // IWYU pragma: keep
#include <pysrf/utils.hpp>

#include <srf/channel/forward.hpp>
#include <srf/channel/ingress.hpp>
#include <srf/channel/status.hpp>
#include <srf/node/edge.hpp>
#include <srf/node/edge_builder.hpp>
#include <srf/node/edge_connector.hpp>
#include <srf/node/edge_registry.hpp>
#include <srf/node/forward.hpp>
#include <srf/node/rx_node.hpp>
#include <srf/node/rx_sink.hpp>
#include <srf/node/rx_source.hpp>
#include <srf/node/sink_properties.hpp>
#include <srf/node/source_properties.hpp>

#include <glog/logging.h>
#include <pybind11/cast.h>
#include <pybind11/gil.h>
#include <pybind11/pybind11.h>  // IWYU pragma: keep
#include <pybind11/pytypes.h>
#include <rxcpp/rx-observable.hpp>
#include <rxcpp/rx-subscriber.hpp>
#include <rxcpp/rx-subscription.hpp>

#include <functional>  // for function, ref
#include <memory>      // for shared_ptr, __shared_ptr_access, dynamic_pointer_cast, allocator, make_shared
#include <ostream>     // for operator<<
#include <typeindex>   // for type_index
#include <utility>     // for move, forward

namespace srf::pysrf {

struct PysrfEdgeAdapterUtil
{
    using source_adaptor_fn_t = std::function<std::shared_ptr<channel::IngressHandle>(
        srf::node::SourcePropertiesBase&, srf::node::SinkPropertiesBase&, std::shared_ptr<channel::IngressHandle>)>;
    using sink_adaptor_fn_t = std::function<std::shared_ptr<channel::IngressHandle>(
        std::type_index, srf::node::SinkPropertiesBase&, std::shared_ptr<channel::IngressHandle> ingress_handle)>;

    template <typename InputT>
    static sink_adaptor_fn_t build_sink_adaptor()
    {
        return [](std::type_index source_type,
                  srf::node::SinkPropertiesBase& sink,
                  std::shared_ptr<channel::IngressHandle> ingress_handle) {
            if (source_type == typeid(PyHolder))
            {
                // Check to see if we have a conversion in pybind11
                if (pybind11::detail::get_type_info(sink.sink_type(true), false))
                {
                    // Shortcut the check to the registered converters
                    auto edge = std::make_shared<node::Edge<PyHolder, InputT>>(
                        std::dynamic_pointer_cast<channel::Ingress<InputT>>(ingress_handle));

                    // Using auto here confuses the lambda's return type with what's returned from
                    // ingress_for_source_type
                    std::shared_ptr<channel::IngressHandle> handle =
                        std::dynamic_pointer_cast<channel::Ingress<PyHolder>>(edge);
                    CHECK(handle);
                    return handle;
                }
            }

            return srf::node::EdgeBuilder::default_ingress_for_source_type(source_type, sink, ingress_handle);
        };
    }

    template <typename OutputT>
    static source_adaptor_fn_t build_source_adaptor()
    {
        return [](srf::node::SourcePropertiesBase& source,
                  srf::node::SinkPropertiesBase& sink,
                  std::shared_ptr<channel::IngressHandle> ingress_handle) {
            // First check if there was a defined converter
            if (node::EdgeRegistry::has_converter(source.source_type(), sink.sink_type()))
            {
                return srf::node::EdgeBuilder::default_ingress_adaptor_for_sink(source, sink, ingress_handle);
            }

            // Check here to see if we can short circuit if both of the types are the same
            if (source.source_type(false) == sink.sink_type(false))
            {
                // Register an edge identity converter
                node::IdentityEdgeConnector<OutputT>::register_converter();

                return srf::node::EdgeBuilder::default_ingress_adaptor_for_sink(source, sink, ingress_handle);
            }

            // By this point several things have happened:
            // 1. Simple shortcut for matching types has failed. SourceT != SinkT
            // 2. We do not have a registered converter
            // 3. Both of our nodes are registered python nodes, but their source and sink types may not be registered

            // We can come up with an edge if one of the following is true:
            // 1. The source is a pybind11::object and the sink is registered with pybind11
            // 2. The sink is a pybind11::object and the source is registered with pybind11
            // 3. Neither is a pybind11::object but both types are registered with pybind11 (worst case, C++ -> py ->
            // C++)

            auto writer_type = source.source_type(true);
            auto reader_type = sink.sink_type(true);

            // Check registrations with pybind11
            auto* writer_typei = pybind11::detail::get_type_info(source.source_type(true), false);
            auto* reader_typei = pybind11::detail::get_type_info(sink.sink_type(true), false);

            // Check if the source is a pybind11::object
            if (writer_type == typeid(PyHolder) && reader_typei)
            {
                return srf::node::EdgeBuilder::ingress_for_source_type(source.source_type(), sink, ingress_handle);
                // return sink_ingress_adaptor_for_source_type(sink, writer_type);
            }

            // Check if the sink is a py::object
            if (reader_type == typeid(PyHolder) && writer_typei)
            {
                // TODO(MDD): To avoid a compound edge here, register an edge converter between OutputT and py::object
                node::EdgeConnector<OutputT, PyHolder>::register_converter();

                // Build the edge with the holder type
                return srf::node::EdgeBuilder::ingress_for_source_type(source.source_type(), sink, ingress_handle);
            }

            // Check if both have been registered with pybind 11
            if (writer_typei && reader_typei)
            {
                // TODO(MDD): Check python types to see if they are compatible

                // Py types can be converted but need a compound edge. Build that here
                // auto py_to_sink_edge = std::dynamic_pointer_cast<channel::Ingress<pybind11::object>>(
                //    sink.ingress_for_source_type(typeid(pybind11::object)));
                auto py_to_sink_edge = std::dynamic_pointer_cast<channel::Ingress<PyHolder>>(
                    srf::node::EdgeBuilder::ingress_for_source_type(typeid(PyHolder), sink, ingress_handle));

                auto source_to_py_edge = std::make_shared<node::Edge<OutputT, PyHolder>>(py_to_sink_edge);

                LOG(WARNING) << "WARNING: A slow edge connection between C++ nodes '" << source.source_type_name()
                             << "' and '" << sink.sink_type_name()
                             << "' has been detected. Performance between "
                                "these nodes can be improved by registering an EdgeConverter at compile time. Without "
                                "this, conversion "
                                "to an intermediate python type will be necessary (i.e. C++ -> Python -> C++).";

                return std::dynamic_pointer_cast<channel::IngressHandle>(source_to_py_edge);
            }

            // Otherwise return base which most likely will fail
            return srf::node::EdgeBuilder::default_ingress_adaptor_for_sink(source, sink, ingress_handle);
        };
    }
};
}  // namespace srf::pysrf