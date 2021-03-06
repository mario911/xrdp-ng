/**
 * xrdp: A Remote Desktop Protocol server.
 *
 * Copyright (C) Jay Sorg 2004-2012
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/**
 *
 * @file sessvc.c
 * @brief Session supervisor
 * @author Simone Fedele
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "os_calls.h"
#include "arch.h"

static int g_term = 0;

/*****************************************************************************/
void  term_signal_handler(int sig)
{
	g_writeln("xrdp-ng-sessvc: term_signal_handler: got signal %d", sig);
	g_term = 1;
}

/*****************************************************************************/
void  nil_signal_handler(int sig)
{
	g_writeln("xrdp-ng-sessvc: nil_signal_handler: got signal %d", sig);
}

/******************************************************************************/
/* chansrv can exit at any time without cleaning up, its an xlib app */
int  chansrv_cleanup(int pid)
{
	char text[256];

	g_snprintf(text, 255, "/tmp/.xrdp/xrdp_chansrv_%8.8x_main_term", pid);

	if (g_file_exist(text))
	{
		g_file_delete(text);
	}

	g_snprintf(text, 255, "/tmp/.xrdp/xrdp_chansrv_%8.8x_thread_done", pid);

	if (g_file_exist(text))
	{
		g_file_delete(text);
	}

	return 0;
}

/******************************************************************************/
int  main(int argc, char **argv)
{
	int ret = 0;
	int chansrv_pid = 0;
	int wm_pid = 0;
	int x_pid = 0;
	int lerror = 0;
	char exe_path[262];

	g_init("xrdp-ng-sessvc");
	g_memset(exe_path, 0, sizeof(exe_path));

	if (argc < 3)
	{
		g_writeln("xrdp-ng-sessvc: exiting, not enough parameters");
		g_deinit();
		return 1;
	}

	g_signal_kill(term_signal_handler); /* SIGKILL */
	g_signal_terminate(term_signal_handler); /* SIGTERM */
	g_signal_user_interrupt(term_signal_handler); /* SIGINT */
	g_signal_pipe(nil_signal_handler); /* SIGPIPE */
	x_pid = g_atoi(argv[1]);
	wm_pid = g_atoi(argv[2]);
	g_writeln("xrdp-ng-sessvc: waiting for X (pid %d) and WM (pid %d)",
			x_pid, wm_pid);
	/* run xrdp-ng-chansrv as a seperate process */
	chansrv_pid = g_fork();

	if (chansrv_pid == -1)
	{
		g_writeln("xrdp-ng-sessvc: fork error");
		g_deinit();
		return 1;
	}
	else if (chansrv_pid == 0) /* child */
	{
		g_set_current_dir(XRDP_SBIN_PATH);
		g_snprintf(exe_path, 261, "%s/xrdp-ng-chansrv", XRDP_SBIN_PATH);
		g_execlp3(exe_path, "xrdp-ng-chansrv", 0);
		/* should not get here */
		g_writeln("xrdp-ng-sessvc: g_execlp3() failed");
		g_deinit();
		return 1;
	}

	lerror = 0;
	/* wait for window manager to get done */
	ret = g_waitpid(wm_pid);

	while ((ret == 0) && !g_term)
	{
		ret = g_waitpid(wm_pid);
		g_sleep(1);
	}

	if (ret < 0) {
		lerror = g_get_errno();
	}

	g_writeln("xrdp-ng-sessvc: WM is dead (waitpid said %d, errno is %d) "
		"exiting...", ret, lerror);

	/* kill channel server */
	g_writeln("xrdp-ng-sessvc: stopping channel server");
	g_sigterm(chansrv_pid);
	ret = g_waitpid(chansrv_pid);

	while ((ret == 0) && !g_term)
	{
		ret = g_waitpid(chansrv_pid);
		g_sleep(1);
	}

	chansrv_cleanup(chansrv_pid);
	/* kill X server */
	g_writeln("xrdp-ng-sessvc: stopping X server");
	g_sigterm(x_pid);
	ret = g_waitpid(x_pid);

	while ((ret == 0) && !g_term)
	{
		ret = g_waitpid(x_pid);
		g_sleep(1);
	}

	g_writeln("xrdp-ng-sessvc: clean exit");
	g_deinit();

	return 0;
}
