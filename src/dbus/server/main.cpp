/*
 * Copyright (C) 2011 Intel Corporation
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) version 3.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301  USA
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <iostream>
#include <locale.h>
#include <glib/gi18n.h>

#include "server.h"
#include "restart.h"
#include "session-common.h"

#include <syncevo/SyncContext.h>
#include <syncevo/SuspendFlags.h>
#include <syncevo/LogRedirect.h>
#include <syncevo/LogSyslog.h>
#include <syncevo/GLibSupport.h>

using namespace SyncEvo;
using namespace GDBusCXX;

namespace {
    GMainLoop *loop = NULL;
    bool shutdownRequested = false;
    const char * const execName = "syncevo-dbus-server";
    const char * const debugEnv = "SYNCEVOLUTION_DEBUG";

void niam(int sig)
{
    shutdownRequested = true;
    SuspendFlags::getSuspendFlags().handleSignal(sig);
    g_main_loop_quit (loop);
}

bool parseDuration(int &duration, const char* value)
{
    if(value == NULL) {
        return false;
    } else if (boost::iequals(value, "unlimited")) {
        duration = -1;
        return true;
    } else if ((duration = atoi(value)) > 0) {
        return true;
    } else {
        return false;
    }
}

} // anonymous namespace

int main(int argc, char **argv, char **envp)
{
    // remember environment for restart
    boost::shared_ptr<Restart> restart;
    restart.reset(new Restart(argv, envp));

    // Internationalization for auto sync messages.
    setlocale(LC_ALL, "");
    bindtextdomain(GETTEXT_PACKAGE,
                   getEnv("SYNCEVOLUTION_LOCALE_DIR", SYNCEVOLUTION_LOCALEDIR));
    bind_textdomain_codeset(GETTEXT_PACKAGE, "UTF-8");
    textdomain(GETTEXT_PACKAGE);

    try {
        gchar *durationString = NULL;
        int duration = 600;
        int logLevel = 1;
        gboolean stdoutEnabled = false;
        gboolean syslogEnabled = true;
        static GOptionEntry entries[] = {
            { "duration", 'd', 0, G_OPTION_ARG_STRING, &durationString, "Shut down automatically when idle for this duration", "seconds/'unlimited'" },
            { "verbosity", 'v', 0, G_OPTION_ARG_INT, &logLevel,
              "Choose amount of output, 0 = no output, 1 = errors, 2 = info, 3 = debug; default is 1.",
              "level" },
            { "stdout", 'o', 0, G_OPTION_ARG_NONE, &stdoutEnabled,
              "Enable printing to stdout (result of operations) and stderr (errors/info/debug).",
              NULL },
            { "no-syslog", 's', G_OPTION_FLAG_REVERSE, G_OPTION_ARG_NONE, &syslogEnabled, "Disable printing to syslog.", NULL },
            { NULL }
        };
        GErrorCXX gerror;
        static GOptionContext *context = g_option_context_new("- SyncEvolution D-Bus Server");
        g_option_context_add_main_entries(context, entries, GETTEXT_PACKAGE);
        bool success = g_option_context_parse(context, &argc, &argv, gerror);
        PlainGStr durationOwner(durationString);
        if (!success) {
            gerror.throwError("parsing command line options");
        }
        if (durationString && !parseDuration(duration, durationString)) {
            SE_THROW(StringPrintf("invalid parameter value '%s' for --duration/-d: must be positive number of seconds or 'unlimited'", durationString));
        }
        Logger::Level level;
        switch (logLevel) {
        case 0:
            level = Logger::NONE;
            break;
        case 1:
            level = Logger::ERROR;
            break;
        case 2:
            level = Logger::INFO;
            break;
        case 3:
            level = Logger::DEBUG;
            break;
        default:
            SE_THROW(StringPrintf("invalid parameter value %d for --debug: must be one of 0, 1, 2 or 3", logLevel));
        }

        // Temporarily set G_DBUS_DEBUG. Hopefully GIO will read and
        // remember it, because we don't want to keep it set
        // permanently, lest it gets passed on to other processes.
        const char *gdbus = getenv("SYNCEVOLUTION_DBUS_SERVER_GDBUS");
        if (gdbus) {
            setenv("G_DBUS_DEBUG", gdbus, 1);
        }

        SyncContext::initMain(execName);

        loop = g_main_loop_new (NULL, FALSE);

        setvbuf(stderr, NULL, _IONBF, 0);
        setvbuf(stdout, NULL, _IONBF, 0);

        // Redirect output and optionally log to syslog.
        LogRedirect redirect(true);
        redirect.setLevel(stdoutEnabled ? level : Logger::NONE);
        std::auto_ptr<LoggerBase> syslogger;
        if (syslogEnabled && level > Logger::NONE) {
            syslogger.reset(new LoggerSyslog(execName));
            syslogger->setLevel(level);
        }

        // syncevo-dbus-server should hardly ever produce output that
        // is relevant for end users, so include the somewhat cryptic
        // process name for developers in this process, and not in
        // syncevo-dbus-helper.
        Logger::setProcessName("syncevo-dbus-server");

        SE_LOG_DEBUG(NULL, NULL, "syncevo-dbus-server: catch SIGINT/SIGTERM in our own shutdown function");
        signal(SIGTERM, niam);
        signal(SIGINT, niam);
        boost::shared_ptr<SuspendFlags::Guard> guard = SuspendFlags::getSuspendFlags().activate();

        DBusErrorCXX err;
        DBusConnectionPtr conn = dbus_get_bus_connection("SESSION",
                                                         SessionCommon::SERVICE_NAME,
                                                         true,
                                                         &err);
        if (!conn) {
            err.throwFailure("dbus_get_bus_connection()", " failed - server already running?");
        }
        // make this object the main owner of the connection
        boost::scoped_ptr<DBusObject> obj(new DBusObject(conn, "foo", "bar", true));

        boost::shared_ptr<SyncEvo::Server> server(new SyncEvo::Server(loop, shutdownRequested, restart, conn, duration));
        server->activate();

#ifdef ENABLE_DBUS_PIM
        boost::shared_ptr<GDBusCXX::DBusObjectHelper> manager(SyncEvo::CreateContactManager(server));
#endif

        if (gdbus) {
            unsetenv("G_DBUS_DEBUG");
        }

        dbus_bus_connection_undelay(conn);
        server->run();
        SE_LOG_DEBUG(NULL, NULL, "cleaning up");
#ifdef ENABLE_DBUS_PIM
        manager.reset();
#endif
        server.reset();
        obj.reset();
        guard.reset();
        SE_LOG_DEBUG(NULL, NULL, "flushing D-Bus connection");
        conn.flush();
        conn.reset();
        SE_LOG_INFO(NULL, NULL, "terminating");
        return 0;
    } catch ( const std::exception &ex ) {
        SE_LOG_ERROR(NULL, NULL, "%s", ex.what());
    } catch (...) {
        SE_LOG_ERROR(NULL, NULL, "unknown error");
    }

    return 1;
}
