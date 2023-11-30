#include <string>
#include <stdexcept>
#include <functional>
#include <optional>
#include <simPlusPlus/Plugin.h>
#include <simPlusPlus/Handles.h>
#include "config.h"
#include "plugin.h"
#include "stubs.h"

using namespace std;
using namespace std::placeholders;

#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>
#include <websocketpp/logger/basic.hpp>
#include <websocketpp/common/cpp11.hpp>
#include <websocketpp/logger/levels.hpp>

namespace websocketpp
{
    namespace log
    {
        template<typename concurrency, typename names>
        class coppeliasim_logger : public basic<concurrency, names>
        {
        public:
            typedef basic<concurrency, names> base;

            coppeliasim_logger<concurrency, names>(channel_type_hint::value hint = channel_type_hint::access)
                    : basic<concurrency, names>(hint), m_channel_type_hint(hint)
            {
            }

            coppeliasim_logger<concurrency, names>(level channels, channel_type_hint::value hint = channel_type_hint::access)
                    : basic<concurrency, names>(channels, hint), m_channel_type_hint(hint)
            {
            }

            static int getVerbosityForChannel(level channel)
            {
                if(channel == elevel::devel)
                    return sim_verbosity_debug;
                else if(channel == elevel::library)
                    return sim_verbosity_debug;
                else if(channel == elevel::info)
                    return sim_verbosity_infos;
                else if(channel == elevel::warn)
                    return sim_verbosity_warnings;
                else if(channel == elevel::rerror)
                    return sim_verbosity_errors;
                else if(channel == elevel::fatal)
                    return sim_verbosity_errors;
                else
                    return sim_verbosity_useglobal;
            }

            void write(level channel, std::string const &msg)
            {
                write(channel, msg.c_str());
            }

            void write(level channel, char const *msg)
            {
                scoped_lock_type lock(base::m_lock);
                if(!this->dynamic_test(channel)) return;
                int verbosity = sim_verbosity_infos;
                if(m_channel_type_hint != channel_type_hint::access)
                    verbosity = getVerbosityForChannel(channel);
                sim::addLog(verbosity, msg);
            }

        private:
            typedef typename base::scoped_lock_type scoped_lock_type;
            channel_type_hint::value m_channel_type_hint;
        };
    } // log
} // websocketpp

struct my_config : public websocketpp::config::asio
{
    typedef websocketpp::log::coppeliasim_logger<concurrency_type, websocketpp::log::elevel> elog_type;
    typedef websocketpp::log::coppeliasim_logger<concurrency_type, websocketpp::log::alevel> alog_type;

    struct my_transport_config : public websocketpp::config::asio::transport_config
    {
        typedef my_config::alog_type alog_type;
        typedef my_config::elog_type elog_type;
    };

    typedef websocketpp::transport::asio::endpoint<my_transport_config> transport_type;
};

typedef websocketpp::server<my_config> my_server;
typedef my_server::connection_type my_connection;

struct server_meta
{
    my_server *srv{nullptr};
    optional<string> openHandler;
    optional<string> failHandler;
    optional<string> closeHandler;
    optional<string> messageHandler;
    optional<string> httpHandler;
    int scriptID;
    int verbose{0};

    ~server_meta() {if(srv)delete srv;}
};

class Plugin : public sim::Plugin
{
public:
    void onInit()
    {
        if(!registerScriptStuff())
            throw runtime_error("failed to register script stuff");

        setExtVersion("WebSocket Plugin");
        setBuildDate(BUILD_DATE);

        if(!sim::getNamedStringParam("simWS.userAgent"))
        {
            vector<int> v{0, 0, 0, sim::getInt32Param(sim_intparam_program_full_version)};
            for(int i = 3; i > 0; i--) v[i - 1] = v[i] / 100;
            for(int i = 0; i < 4; i++) v[i] = v[i] % 100;
            auto p = sim::getInt32Param(sim_intparam_platform);
            sim::setNamedStringParam("simWS.userAgent",
                sim::util::sprintf("CoppeliaSim/%d.%d.%drev%d %s",
                    v[0], v[1], v[2], v[3],
                    p == 0 ? "Windows" :
                    p == 1 ? "macOS" :
                    p == 2 ? "Linux" :
                    "Unknown-platform"
                )
            );
        }
    }

    void onCleanup()
    {
    }

    void onInstancePass(const sim::InstancePassFlags &flags)
    {
        for(auto meta : handles.all())
            meta->srv->poll();
    }

    void onScriptStateDestroyed(int scriptID)
    {
        for(auto meta : handles.find(scriptID))
        {
            meta->srv->stop_listening();
            meta->srv->stop_perpetual();
            handles.remove(meta);
            delete meta;
        }
    }

    void onWSOpen(server_meta *meta, websocketpp::connection_hdl hdl)
    {
        sim::addLog(sim_verbosity_debug, "onWSOpen");

        if(!meta->openHandler) return;

        openCallback_in in;
        in.serverHandle = handles.add(meta, meta->scriptID);
        in.connectionHandle = connHandles.add(hdl, meta->scriptID);
        openCallback_out out;
        openCallback(meta->scriptID, meta->openHandler->c_str(), &in, &out);
    }

    void onWSFail(server_meta *meta, websocketpp::connection_hdl hdl)
    {
        sim::addLog(sim_verbosity_debug, "onWSFail");

        if(!meta->failHandler) return;

        failCallback_in in;
        in.serverHandle = handles.add(meta, meta->scriptID);
        in.connectionHandle = connHandles.add(hdl, meta->scriptID);
        failCallback_out out;
        failCallback(meta->scriptID, meta->failHandler->c_str(), &in, &out);
    }

    void onWSClose(server_meta *meta, websocketpp::connection_hdl hdl)
    {
        sim::addLog(sim_verbosity_debug, "onWSClose");

        if(!meta->closeHandler) return;

        closeCallback_in in;
        in.serverHandle = handles.add(meta, meta->scriptID);
        in.connectionHandle = connHandles.add(hdl, meta->scriptID);
        closeCallback_out out;
        closeCallback(meta->scriptID, meta->closeHandler->c_str(), &in, &out);
    }

    void onWSMessage(server_meta *meta, websocketpp::connection_hdl hdl, my_server::message_ptr msg)
    {
        sim::addLog(sim_verbosity_debug, "onWSMessage: %s", msg->get_payload());

        if(!meta->messageHandler) return;

        messageCallback_in in;
        in.serverHandle = handles.add(meta, meta->scriptID);
        in.connectionHandle = connHandles.add(hdl, meta->scriptID);
        in.data = msg->get_payload();
        messageCallback_out out;
        messageCallback(meta->scriptID, meta->messageHandler->c_str(), &in, &out);
    }

    void onWSHTTP(server_meta *meta, websocketpp::connection_hdl hdl)
    {
        my_server::connection_ptr con = meta->srv->get_con_from_hdl(hdl);

        sim::addLog(sim_verbosity_debug, "onWSHTTP: %s", con->get_resource());

        if(!meta->httpHandler)
        {
            con->set_status(websocketpp::http::status_code::not_found);
            return;
        }

        if(auto h = hdl.lock())
        {
            httpCallback_in in;
            in.serverHandle = handles.add(meta, meta->scriptID);
            in.connectionHandle = connHandles.add(hdl, meta->scriptID);
            in.resource = con->get_resource();
            in.data = con->get_request_body();
            httpCallback_out out;
            httpCallback(meta->scriptID, meta->httpHandler->c_str(), &in, &out);
            con->set_status(static_cast<websocketpp::http::status_code::value>(out.status));
            con->set_body(out.data);
        }
        else
        {
            throw runtime_error("connection_hdl weak_ptr expired");
        }
    }

    void start(start_in *in, start_out *out)
    {
        auto meta = new server_meta;
        meta->srv = new my_server;
        meta->srv->set_user_agent(*sim::getNamedStringParam("simWS.userAgent"));
        auto verbose = sim::getNamedInt32Param("simWS.verbose");
        if(verbose)
            meta->verbose = *verbose;
        if(meta->verbose > 0)
            meta->srv->set_access_channels(websocketpp::log::alevel::all ^ websocketpp::log::alevel::frame_payload);
        else
            meta->srv->set_access_channels(websocketpp::log::alevel::none);
        meta->srv->init_asio();
        meta->srv->set_open_handler(bind(&Plugin::onWSOpen, this, meta, _1));
        meta->srv->set_fail_handler(bind(&Plugin::onWSFail, this, meta, _1));
        meta->srv->set_close_handler(bind(&Plugin::onWSClose, this, meta, _1));
        meta->srv->set_message_handler(bind(&Plugin::onWSMessage, this, meta, _1, _2));
        meta->srv->set_http_handler(bind(&Plugin::onWSHTTP, this, meta, _1));
        meta->srv->listen(in->listenPort);
        meta->srv->start_accept();
        meta->scriptID = in->_.scriptID;
        out->serverHandle = handles.add(meta, in->_.scriptID);
    }

    void setOpenHandler(setOpenHandler_in *in, setOpenHandler_out *out)
    {
        auto meta = handles.get(in->serverHandle);
        meta->openHandler = in->callbackFn;
    }

    void setFailHandler(setFailHandler_in *in, setFailHandler_out *out)
    {
        auto meta = handles.get(in->serverHandle);
        meta->failHandler = in->callbackFn;
    }

    void setCloseHandler(setCloseHandler_in *in, setCloseHandler_out *out)
    {
        auto meta = handles.get(in->serverHandle);
        meta->closeHandler = in->callbackFn;
    }

    void setMessageHandler(setMessageHandler_in *in, setMessageHandler_out *out)
    {
        auto meta = handles.get(in->serverHandle);
        meta->messageHandler = in->callbackFn;
    }

    void setHTTPHandler(setHTTPHandler_in *in, setHTTPHandler_out *out)
    {
        auto meta = handles.get(in->serverHandle);
        meta->httpHandler = in->callbackFn;
    }

    void send(send_in *in, send_out *out)
    {
        if(in->opcode < 0 || in->opcode > 2)
            throw sim::exception("invalid opcode: %d", in->opcode);
        auto meta = handles.get(in->serverHandle);
        auto hdl = connHandles.get(in->connectionHandle);
        if(auto h = hdl.lock())
        {
            auto c = meta->srv->get_con_from_hdl(hdl);
            meta->srv->send(c, in->data, static_cast<websocketpp::frame::opcode::value>(in->opcode));
        }
        else
        {
            throw runtime_error("connection_hdl weak_ptr expired");
        }
    }

    void stop(stop_in *in, stop_out *out)
    {
        auto meta = handles.get(in->serverHandle);
        meta->srv->stop_listening();
        meta->srv->stop_perpetual();
        handles.remove(meta);
        delete meta;
    }

private:
    sim::Handles<server_meta*> handles{"simWS.Server"};
    sim::WeakHandles<websocketpp::connection_hdl> connHandles{"simWS.Connection"};
};

SIM_PLUGIN(Plugin)
#include "stubsPlusPlus.cpp"
