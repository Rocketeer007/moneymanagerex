/*******************************************************
 Copyright (C) 2014 James Higley
 Copyright (C) 2014 Guan Lisheng (guanlisheng@gmail.com)

 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 2 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 ********************************************************/

#include "webserver.h"
#include "defs.h"
#include "platfdep.h"
#include "paths.h"
#include "mongoose/mongoose.h"
#include "singleton.h"
#include "Model_Setting.h"
#include "Model_Report.h"
#include "Model_Setting.h"
#include "Model_Infotable.h"

static struct mg_serve_http_opts s_http_server_opts;

static void handle_sql(struct mg_connection* nc, struct http_message* hm)
{
    char query[0xffff];
    mg_get_http_var(&hm->query_string, "query", query, sizeof(query));
    std::cout<<query<<std::endl;

    StringBuffer json_buffer;
    PrettyWriter<StringBuffer> json_writer(json_buffer);

    json_writer.StartObject();

    json_writer.Key("query");
    json_writer.String(wxString(query).c_str());

    Model_Report::instance().get_objects_from_sql(wxString(query), json_writer);

    for (const auto & r : Model_Setting::instance().all())
    {
        json_writer.Key(r.SETTINGNAME.c_str());
        json_writer.String(r.SETTINGVALUE.c_str());
    }

    for (const auto & r : Model_Infotable::instance().all())
    {
        json_writer.Key(r.INFONAME.c_str());
        json_writer.String(r.INFOVALUE.c_str());
    }
    
    json_writer.EndObject();

    const char* str = json_buffer.GetString();
    std::cout<<str<<std::endl;

    mg_printf(nc, "HTTP/1.1 200 OK\r\n"
                "Content-Type: application/json; charset=utf-8\r\n"
                "Content-Length: %zu\r\n\r\n%s", sizeof(str) / sizeof(char), str);
}

static void ev_handler(struct mg_connection *nc, int ev, void *ev_data) 
{
    struct http_message *hm = (struct http_message *) ev_data;

    switch (ev)
    {
        case MG_EV_HTTP_REQUEST:
            if (mg_vcmp(&hm->uri, "/api/v1/sql") == 0)
            {
                handle_sql(nc, hm);
            }
            else
            {
                mg_serve_http(nc, hm, s_http_server_opts); /* Serve static content */
            }
            break;
        default:
            break;
    }
}

WebServerThread::WebServerThread(): wxThread()
{
}

WebServerThread::~WebServerThread()
{
}

wxThread::ExitCode WebServerThread::Entry()
{
    // Get user setting
    int webserverPort = Model_Setting::instance().GetIntSetting("WEBSERVERPORT", 8080);
	const wxString& strPort = wxString::Format(":%d", webserverPort);
	
	int webserverSslPort = Model_Setting::instance().GetIntSetting("WEBSERVERSSLPORT", 8089); // TODO: default to 8443
	const wxString& strSslPort = wxString::Format(":%d", webserverSslPort);
	wxString strSslCert = Model_Setting::instance().GetStringSetting("WEBSERVERSSLCERTPATH", "C:/Temp/cert.pem"); // TODO: default to blank
	wxString strSslKey = Model_Setting::instance().GetStringSetting("WEBSERVERSSLCERTKEY", "C:/Temp/key.pem"); // TODO: default to blank

    // Create and configure the server
    struct mg_mgr mgr;

    mg_mgr_init(&mgr, NULL);

	if (Model_Setting::instance().GetBoolSetting("ENABLEWEBSERVER", false)) {
		wxLogDebug("Starting HTTP server on port %s\n", strPort);
		struct mg_connection* nc = mg_bind(&mgr, strPort.c_str(), ev_handler);
		if (nc == nullptr)
		{
			wxLogDebug(wxString::Format("mg_bind(%s) failed", strPort));
			mg_mgr_free(&mgr);
			return (wxThread::ExitCode) - 1;
		}

		mg_set_protocol_http_websocket(nc);
	}

	if (Model_Setting::instance().GetBoolSetting("ENABLEWEBSERVERSSL", true)) { // TODO: default to FALSE
		struct mg_bind_opts bind_opts;
		memset(&bind_opts, 0, sizeof(bind_opts));
		bind_opts.ssl_cert = strSslCert.c_str();
		bind_opts.ssl_key = strSslKey.c_str();

		wxLogDebug("Starting SSL server on port %s, cert from %s, key from %s\n",
			strSslPort, bind_opts.ssl_cert, bind_opts.ssl_key);
		struct mg_connection* nc_ssl = mg_bind_opt(&mgr, strSslPort.c_str(), ev_handler, bind_opts);
		if (nc_ssl == nullptr)
		{
			wxLogDebug(wxString::Format("mg_bind_opts(%s) failed", strSslPort.c_str()));
			mg_mgr_free(&mgr);
			return (wxThread::ExitCode) - 1;
		}

		mg_set_protocol_http_websocket(nc_ssl);
	}

    std::string document_root(wxFileName(mmex::getReportFullFileName("index")).GetPath().c_str());
    s_http_server_opts.document_root = document_root.c_str();
    s_http_server_opts.enable_directory_listing = "yes";

    wxSetWorkingDirectory(wxString(s_http_server_opts.document_root));

    // Serve requests 
    while (IsAlive())
    {
        mg_mgr_poll(&mgr, 1000);
    }

    // Cleanup, and free server instance
    mg_mgr_free(&mgr);

    return nullptr;
}

Mongoose_Service::Mongoose_Service(): m_thread(0)
{
}

Mongoose_Service::~Mongoose_Service()
{}

Mongoose_Service&
Mongoose_Service::instance()
{
    return Singleton<Mongoose_Service>::instance();
}

int Mongoose_Service::open()
{
    this->svc();
    return 0;
}

int Mongoose_Service::svc()
{
	bool isEnabled = Model_Setting::instance().GetBoolSetting("ENABLEWEBSERVER", false)
		|| Model_Setting::instance().GetBoolSetting("ENABLEWEBSERVERSSL", false);

    if (isEnabled)
    {
        m_thread = new WebServerThread();
        if (m_thread->Run() == wxTHREAD_NO_ERROR)
        {
            wxLogDebug("Mongoose Service started");
        }
        else
        {
            wxLogDebug("Can't create the web server thread!");
            delete m_thread;
            m_thread = 0;
        }
    }
    return 0;
}

int Mongoose_Service::stop()
{
    if (m_thread != 0)
    {
        if (m_thread->Delete() == wxTHREAD_NO_ERROR)
        {
            wxLogDebug("Mongoose Service ended.");
        }
        else
        {
            wxLogError("Can't delete the thread!");
        }
    }
    return 0;
}
