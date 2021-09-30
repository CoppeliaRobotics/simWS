#include <string>
#include <stdexcept>
#include <functional>
#include <optional>
#include "simPlusPlus/Plugin.h"
#include "simPlusPlus/Handle.h"
#include "config.h"
#include "plugin.h"
#include "stubs.h"

using namespace std;
using namespace std::placeholders;

#define ASIO_STANDALONE
#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>
typedef websocketpp::server<websocketpp::config::asio> server;
typedef server::connection_type connection;

struct server_meta
{
    server *server{nullptr};
    optional<string> openHandler;
    optional<string> failHandler;
    optional<string> closeHandler;
    optional<string> messageHandler;
    optional<string> httpHandler;
    int scriptID;
    int verbose{0};

    ~server_meta() {if(server)delete server;}
};

using sim::Handle;
using sim::Handles;
typedef Handle<server_meta> ServerHandle;
typedef Handle<connection> ConnectionHandle;
template<> string ServerHandle::tag() { return "simWS.Server"; }
template<> string ConnectionHandle::tag() { return "simWS.Connection"; }

class Plugin : public sim::Plugin
{
public:
    void onStart()
    {
        if(!registerScriptStuff())
            throw runtime_error("failed to register script stuff");

        setExtVersion("WebSockets Plugin");
        setBuildDate(BUILD_DATE);

        if(!sim::getStringNamedParam("simWS.userAgent"))
        {
            vector<int> v{0, 0, 0, sim::getInt32Parameter(sim_intparam_program_full_version)};
            for(int i = 3; i > 0; i--) v[i - 1] = v[i] / 100;
            for(int i = 0; i < 4; i++) v[i] = v[i] % 100;
            auto p = sim::getInt32Parameter(sim_intparam_platform);
            sim::setStringNamedParam("simWS.userAgent",
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

    void onEnd()
    {
    }

    void onInstancePass(const sim::InstancePassFlags &flags)
    {
        for(auto meta : handles.all())
            meta->server->poll();
    }

    void onScriptStateDestroyed(int scriptID)
    {
        for(auto meta : handles.find(scriptID))
        {
            meta->server->stop_listening();
            meta->server->stop_perpetual();
            handles.remove(meta);
            delete meta;
        }
    }

    void onWSOpen(server_meta *meta, websocketpp::connection_hdl hdl)
    {
        sim::addLog(sim_verbosity_debug, "onWSOpen");

        if(!meta->openHandler) return;

        if(auto h = hdl.lock())
        {
            openCallback_in in;
            in.serverHandle = ServerHandle::str(meta);
            in.connectionHandle = ConnectionHandle::str(meta->server->get_con_from_hdl(hdl).get());
            openCallback_out out;
            openCallback(meta->scriptID, meta->openHandler->c_str(), &in, &out);
        }
        else
        {
            throw runtime_error("connection_hdl weak_ptr expired");
        }
    }

    void onWSFail(server_meta *meta, websocketpp::connection_hdl hdl)
    {
        sim::addLog(sim_verbosity_debug, "onWSFail");

        if(!meta->failHandler) return;

        if(auto h = hdl.lock())
        {
            failCallback_in in;
            in.serverHandle = ServerHandle::str(meta);
            in.connectionHandle = ConnectionHandle::str(meta->server->get_con_from_hdl(hdl).get());
            failCallback_out out;
            failCallback(meta->scriptID, meta->failHandler->c_str(), &in, &out);
        }
        else
        {
            throw runtime_error("connection_hdl weak_ptr expired");
        }
    }

    void onWSClose(server_meta *meta, websocketpp::connection_hdl hdl)
    {
        sim::addLog(sim_verbosity_debug, "onWSClose");

        if(!meta->closeHandler) return;

        if(auto h = hdl.lock())
        {
            closeCallback_in in;
            in.serverHandle = ServerHandle::str(meta);
            in.connectionHandle = ConnectionHandle::str(meta->server->get_con_from_hdl(hdl).get());
            closeCallback_out out;
            closeCallback(meta->scriptID, meta->closeHandler->c_str(), &in, &out);
        }
        else
        {
            throw runtime_error("connection_hdl weak_ptr expired");
        }
    }

    void onWSMessage(server_meta *meta, websocketpp::connection_hdl hdl, server::message_ptr msg)
    {
        sim::addLog(sim_verbosity_debug, "onWSMessage: %s", msg->get_payload());

        if(!meta->messageHandler) return;

        if(auto h = hdl.lock())
        {
            messageCallback_in in;
            in.serverHandle = ServerHandle::str(meta);
            in.connectionHandle = ConnectionHandle::str(meta->server->get_con_from_hdl(hdl).get());
            in.data = msg->get_payload();
            messageCallback_out out;
            messageCallback(meta->scriptID, meta->messageHandler->c_str(), &in, &out);
        }
        else
        {
            throw runtime_error("connection_hdl weak_ptr expired");
        }
    }

    void onWSHTTP(server_meta *meta, websocketpp::connection_hdl hdl)
    {
        server::connection_ptr con = meta->server->get_con_from_hdl(hdl);

        sim::addLog(sim_verbosity_debug, "onWSHTTP: %s", con->get_resource());

        if(!meta->httpHandler)
        {
            con->set_status(websocketpp::http::status_code::not_found);
            return;
        }

        if(auto h = hdl.lock())
        {
            httpCallback_in in;
            in.serverHandle = ServerHandle::str(meta);
            in.connectionHandle = ConnectionHandle::str(meta->server->get_con_from_hdl(hdl).get());
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
        meta->server = new ::server;
        meta->server->set_user_agent(*sim::getStringNamedParam("simWS.userAgent"));
        auto verbose = sim::getStringNamedParam("simWS.verbose");
        if(verbose)
            meta->verbose = stoi(*verbose);
        if(meta->verbose > 0)
            meta->server->set_access_channels(websocketpp::log::alevel::all ^ websocketpp::log::alevel::frame_payload);
        else
            meta->server->set_access_channels(websocketpp::log::alevel::none);
        meta->server->init_asio();
        meta->server->set_open_handler(bind(&Plugin::onWSOpen, this, meta, _1));
        meta->server->set_fail_handler(bind(&Plugin::onWSFail, this, meta, _1));
        meta->server->set_close_handler(bind(&Plugin::onWSClose, this, meta, _1));
        meta->server->set_message_handler(bind(&Plugin::onWSMessage, this, meta, _1, _2));
        meta->server->set_http_handler(bind(&Plugin::onWSHTTP, this, meta, _1));
        meta->server->listen(in->listenPort);
        meta->server->start_accept();
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
        auto c = shared_ptr<connection>(ConnectionHandle::obj(in->connectionHandle), [=](connection*){});
        meta->server->send(c, in->data, static_cast<websocketpp::frame::opcode::value>(in->opcode));
    }

    void stop(stop_in *in, stop_out *out)
    {
        auto meta = handles.get(in->serverHandle);
        meta->server->stop_listening();
        meta->server->stop_perpetual();
        handles.remove(meta);
        delete meta;
    }

private:
    Handles<server_meta> handles;
};

SIM_PLUGIN(PLUGIN_NAME, PLUGIN_VERSION, Plugin)
#include "stubsPlusPlus.cpp"
