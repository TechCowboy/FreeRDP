/**
 * WinPR: Windows Portable Runtime
 * Process Thread Functions
 *
 * Copyright 2012 Marc-Andre Moreau <marcandre.moreau@gmail.com>
 * Copyright 2014 DI (FH) Martin Haimberger <martin.haimberger@thincast.com>
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

#include <winpr/config.h>

#include <winpr/handle.h>
#include "../handle/nonehandle.h"

#include <winpr/thread.h>
#include <winpr/wlog.h>

/**
 * CreateProcessA
 * CreateProcessW
 * CreateProcessAsUserA
 * CreateProcessAsUserW
 * ExitProcess
 * GetCurrentProcess
 * GetCurrentProcessId
 * GetExitCodeProcess
 * GetProcessHandleCount
 * GetProcessId
 * GetProcessIdOfThread
 * GetProcessMitigationPolicy
 * GetProcessTimes
 * GetProcessVersion
 * OpenProcess
 * OpenProcessToken
 * ProcessIdToSessionId
 * SetProcessAffinityUpdateMode
 * SetProcessMitigationPolicy
 * SetProcessShutdownParameters
 * TerminateProcess
 */

#ifndef _WIN32

#include <winpr/assert.h>
#include <winpr/crt.h>
#include <winpr/path.h>
#include <winpr/environment.h>

#include <grp.h>

#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>

#ifdef __linux__
#include <sys/syscall.h>
#include <fcntl.h>
#include <errno.h>
#endif /* __linux__ */

#include "thread.h"

#include "../security/security.h"

#ifndef NSIG
#ifdef SIGMAX
#define NSIG SIGMAX
#else
#define NSIG 64
#endif
#endif

/**
 * If the file name does not contain a directory path, the system searches for the executable file
 * in the following sequence:
 *
 * 1) The directory from which the application loaded.
 * 2) The current directory for the parent process.
 * 3) The 32-bit Windows system directory. Use the GetSystemDirectory function to get the path of
 * this directory. 4) The 16-bit Windows system directory. There is no function that obtains the
 * path of this directory, but it is searched. The name of this directory is System. 5) The Windows
 * directory. Use the GetWindowsDirectory function to get the path of this directory. 6) The
 * directories that are listed in the PATH environment variable. Note that this function does not
 * search the per-application path specified by the App Paths registry key. To include this
 * per-application path in the search sequence, use the ShellExecute function.
 */

static char* FindApplicationPath(char* application)
{
	LPCSTR pathName = "PATH";
	char* path = NULL;
	char* save = NULL;
	DWORD nSize = 0;
	LPSTR lpSystemPath = NULL;
	char* filename = NULL;

	if (!application)
		return NULL;

	if (application[0] == '/')
		return _strdup(application);

	nSize = GetEnvironmentVariableA(pathName, NULL, 0);

	if (!nSize)
		return _strdup(application);

	lpSystemPath = (LPSTR)malloc(nSize);

	if (!lpSystemPath)
		return NULL;

	if (GetEnvironmentVariableA(pathName, lpSystemPath, nSize) != nSize - 1)
	{
		free(lpSystemPath);
		return NULL;
	}

	save = NULL;
	path = strtok_s(lpSystemPath, ":", &save);

	while (path)
	{
		filename = GetCombinedPath(path, application);

		if (winpr_PathFileExists(filename))
		{
			break;
		}

		free(filename);
		filename = NULL;
		path = strtok_s(NULL, ":", &save);
	}

	free(lpSystemPath);
	return filename;
}

static HANDLE CreateProcessHandle(pid_t pid);
static BOOL ProcessHandleCloseHandle(HANDLE handle);

static BOOL CreateProcessExA(HANDLE hToken, WINPR_ATTR_UNUSED DWORD dwLogonFlags,
                             LPCSTR lpApplicationName, WINPR_ATTR_UNUSED LPSTR lpCommandLine,
                             WINPR_ATTR_UNUSED LPSECURITY_ATTRIBUTES lpProcessAttributes,
                             WINPR_ATTR_UNUSED LPSECURITY_ATTRIBUTES lpThreadAttributes,
                             WINPR_ATTR_UNUSED BOOL bInheritHandles,
                             WINPR_ATTR_UNUSED DWORD dwCreationFlags, LPVOID lpEnvironment,
                             LPCSTR lpCurrentDirectory, LPSTARTUPINFOA lpStartupInfo,
                             LPPROCESS_INFORMATION lpProcessInformation)
{
	pid_t pid = 0;
	int numArgs = 0;
	LPSTR* pArgs = NULL;
	char** envp = NULL;
	char* filename = NULL;
	HANDLE thread = NULL;
	HANDLE process = NULL;
	WINPR_ACCESS_TOKEN* token = NULL;
	LPTCH lpszEnvironmentBlock = NULL;
	BOOL ret = FALSE;
	sigset_t oldSigMask;
	sigset_t newSigMask;
	BOOL restoreSigMask = FALSE;
	numArgs = 0;
	lpszEnvironmentBlock = NULL;
	/* https://docs.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-createprocessa
	 */
	if (lpCommandLine)
		pArgs = CommandLineToArgvA(lpCommandLine, &numArgs);
	else
		pArgs = CommandLineToArgvA(lpApplicationName, &numArgs);

	if (!pArgs)
		return FALSE;

	token = (WINPR_ACCESS_TOKEN*)hToken;

	if (lpEnvironment)
	{
		envp = EnvironmentBlockToEnvpA(lpEnvironment);
	}
	else
	{
		lpszEnvironmentBlock = GetEnvironmentStrings();

		if (!lpszEnvironmentBlock)
			goto finish;

		envp = EnvironmentBlockToEnvpA(lpszEnvironmentBlock);
	}

	if (!envp)
		goto finish;

	filename = FindApplicationPath(pArgs[0]);

	if (NULL == filename)
		goto finish;

	/* block all signals so that the child can safely reset the caller's handlers */
	sigfillset(&newSigMask);
	restoreSigMask = !pthread_sigmask(SIG_SETMASK, &newSigMask, &oldSigMask);
	/* fork and exec */
	pid = fork();

	if (pid < 0)
	{
		/* fork failure */
		goto finish;
	}

	if (pid == 0)
	{
		/* child process */
#ifndef __sun
		int maxfd = 0;
#endif
		sigset_t set = { 0 };
		struct sigaction act = { 0 };
		/* set default signal handlers */
		act.sa_handler = SIG_DFL;
		act.sa_flags = 0;
		sigemptyset(&act.sa_mask);

		for (int sig = 1; sig < NSIG; sig++)
			sigaction(sig, &act, NULL);

		/* unblock all signals */
		sigfillset(&set);
		pthread_sigmask(SIG_UNBLOCK, &set, NULL);

		if (lpStartupInfo)
		{
			int handle_fd = 0;
			handle_fd = winpr_Handle_getFd(lpStartupInfo->hStdOutput);

			if (handle_fd != -1)
				dup2(handle_fd, STDOUT_FILENO);

			handle_fd = winpr_Handle_getFd(lpStartupInfo->hStdError);

			if (handle_fd != -1)
				dup2(handle_fd, STDERR_FILENO);

			handle_fd = winpr_Handle_getFd(lpStartupInfo->hStdInput);

			if (handle_fd != -1)
				dup2(handle_fd, STDIN_FILENO);
		}

#ifdef __sun
		closefrom(3);
#else
#ifdef F_MAXFD // on some BSD derivates
		maxfd = fcntl(0, F_MAXFD);
#else
		{
			const long rc = sysconf(_SC_OPEN_MAX);
			if ((rc < INT32_MIN) || (rc > INT32_MAX))
				goto finish;
			maxfd = (int)rc;
		}
#endif

		for (int fd = 3; fd < maxfd; fd++)
			close(fd);

#endif // __sun

		if (token)
		{
			if (token->GroupId)
			{
				int rc = setgid((gid_t)token->GroupId);

				if (rc < 0)
				{
				}
				else
				{
					initgroups(token->Username, (gid_t)token->GroupId);
				}
			}

			if (token->UserId)
			{
				int rc = setuid((uid_t)token->UserId);
				if (rc != 0)
					goto finish;
			}
		}

		/* TODO: add better cwd handling and error checking */
		if (lpCurrentDirectory && strlen(lpCurrentDirectory) > 0)
		{
			int rc = chdir(lpCurrentDirectory);
			if (rc != 0)
				goto finish;
		}

		if (execve(filename, pArgs, envp) < 0)
		{
			/* execve failed - end the process */
			_exit(1);
		}
	}
	else
	{
		/* parent process */
	}

	process = CreateProcessHandle(pid);

	if (!process)
	{
		goto finish;
	}

	thread = CreateNoneHandle();

	if (!thread)
	{
		ProcessHandleCloseHandle(process);
		goto finish;
	}

	lpProcessInformation->hProcess = process;
	lpProcessInformation->hThread = thread;
	lpProcessInformation->dwProcessId = (DWORD)pid;
	lpProcessInformation->dwThreadId = (DWORD)pid;
	ret = TRUE;
finish:

	/* restore caller's original signal mask */
	if (restoreSigMask)
		pthread_sigmask(SIG_SETMASK, &oldSigMask, NULL);

	free(filename);
	free((void*)pArgs);

	if (lpszEnvironmentBlock)
		FreeEnvironmentStrings(lpszEnvironmentBlock);

	if (envp)
	{
		int i = 0;

		while (envp[i])
		{
			free(envp[i]);
			i++;
		}

		free((void*)envp);
	}

	return ret;
}

BOOL CreateProcessA(LPCSTR lpApplicationName, LPSTR lpCommandLine,
                    LPSECURITY_ATTRIBUTES lpProcessAttributes,
                    LPSECURITY_ATTRIBUTES lpThreadAttributes, BOOL bInheritHandles,
                    DWORD dwCreationFlags, LPVOID lpEnvironment, LPCSTR lpCurrentDirectory,
                    LPSTARTUPINFOA lpStartupInfo, LPPROCESS_INFORMATION lpProcessInformation)
{
	return CreateProcessExA(NULL, 0, lpApplicationName, lpCommandLine, lpProcessAttributes,
	                        lpThreadAttributes, bInheritHandles, dwCreationFlags, lpEnvironment,
	                        lpCurrentDirectory, lpStartupInfo, lpProcessInformation);
}

BOOL CreateProcessW(WINPR_ATTR_UNUSED LPCWSTR lpApplicationName,
                    WINPR_ATTR_UNUSED LPWSTR lpCommandLine,
                    WINPR_ATTR_UNUSED LPSECURITY_ATTRIBUTES lpProcessAttributes,
                    WINPR_ATTR_UNUSED LPSECURITY_ATTRIBUTES lpThreadAttributes,
                    WINPR_ATTR_UNUSED BOOL bInheritHandles, WINPR_ATTR_UNUSED DWORD dwCreationFlags,
                    WINPR_ATTR_UNUSED LPVOID lpEnvironment,
                    WINPR_ATTR_UNUSED LPCWSTR lpCurrentDirectory,
                    WINPR_ATTR_UNUSED LPSTARTUPINFOW lpStartupInfo,
                    WINPR_ATTR_UNUSED LPPROCESS_INFORMATION lpProcessInformation)
{
	WLog_ERR("TODO", "TODO: implement");
	return FALSE;
}

BOOL CreateProcessAsUserA(HANDLE hToken, LPCSTR lpApplicationName, LPSTR lpCommandLine,
                          LPSECURITY_ATTRIBUTES lpProcessAttributes,
                          LPSECURITY_ATTRIBUTES lpThreadAttributes, BOOL bInheritHandles,
                          DWORD dwCreationFlags, LPVOID lpEnvironment, LPCSTR lpCurrentDirectory,
                          LPSTARTUPINFOA lpStartupInfo, LPPROCESS_INFORMATION lpProcessInformation)
{
	return CreateProcessExA(hToken, 0, lpApplicationName, lpCommandLine, lpProcessAttributes,
	                        lpThreadAttributes, bInheritHandles, dwCreationFlags, lpEnvironment,
	                        lpCurrentDirectory, lpStartupInfo, lpProcessInformation);
}

BOOL CreateProcessAsUserW(WINPR_ATTR_UNUSED HANDLE hToken,
                          WINPR_ATTR_UNUSED LPCWSTR lpApplicationName,
                          WINPR_ATTR_UNUSED LPWSTR lpCommandLine,
                          WINPR_ATTR_UNUSED LPSECURITY_ATTRIBUTES lpProcessAttributes,
                          WINPR_ATTR_UNUSED LPSECURITY_ATTRIBUTES lpThreadAttributes,
                          WINPR_ATTR_UNUSED BOOL bInheritHandles,
                          WINPR_ATTR_UNUSED DWORD dwCreationFlags,
                          WINPR_ATTR_UNUSED LPVOID lpEnvironment,
                          WINPR_ATTR_UNUSED LPCWSTR lpCurrentDirectory,
                          WINPR_ATTR_UNUSED LPSTARTUPINFOW lpStartupInfo,
                          WINPR_ATTR_UNUSED LPPROCESS_INFORMATION lpProcessInformation)
{
	WLog_ERR("TODO", "TODO: implement");
	return FALSE;
}

BOOL CreateProcessWithLogonA(
    WINPR_ATTR_UNUSED LPCSTR lpUsername, WINPR_ATTR_UNUSED LPCSTR lpDomain,
    WINPR_ATTR_UNUSED LPCSTR lpPassword, WINPR_ATTR_UNUSED DWORD dwLogonFlags,
    WINPR_ATTR_UNUSED LPCSTR lpApplicationName, WINPR_ATTR_UNUSED LPSTR lpCommandLine,
    WINPR_ATTR_UNUSED DWORD dwCreationFlags, WINPR_ATTR_UNUSED LPVOID lpEnvironment,
    WINPR_ATTR_UNUSED LPCSTR lpCurrentDirectory, WINPR_ATTR_UNUSED LPSTARTUPINFOA lpStartupInfo,
    WINPR_ATTR_UNUSED LPPROCESS_INFORMATION lpProcessInformation)
{
	WLog_ERR("TODO", "TODO: implement");
	return FALSE;
}

BOOL CreateProcessWithLogonW(
    WINPR_ATTR_UNUSED LPCWSTR lpUsername, WINPR_ATTR_UNUSED LPCWSTR lpDomain,
    WINPR_ATTR_UNUSED LPCWSTR lpPassword, WINPR_ATTR_UNUSED DWORD dwLogonFlags,
    WINPR_ATTR_UNUSED LPCWSTR lpApplicationName, WINPR_ATTR_UNUSED LPWSTR lpCommandLine,
    WINPR_ATTR_UNUSED DWORD dwCreationFlags, WINPR_ATTR_UNUSED LPVOID lpEnvironment,
    WINPR_ATTR_UNUSED LPCWSTR lpCurrentDirectory, WINPR_ATTR_UNUSED LPSTARTUPINFOW lpStartupInfo,
    WINPR_ATTR_UNUSED LPPROCESS_INFORMATION lpProcessInformation)
{
	WLog_ERR("TODO", "TODO: implement");
	return FALSE;
}

BOOL CreateProcessWithTokenA(WINPR_ATTR_UNUSED HANDLE hToken, WINPR_ATTR_UNUSED DWORD dwLogonFlags,
                             LPCSTR lpApplicationName, LPSTR lpCommandLine, DWORD dwCreationFlags,
                             LPVOID lpEnvironment, LPCSTR lpCurrentDirectory,
                             LPSTARTUPINFOA lpStartupInfo,
                             LPPROCESS_INFORMATION lpProcessInformation)
{
	return CreateProcessExA(NULL, 0, lpApplicationName, lpCommandLine, NULL, NULL, FALSE,
	                        dwCreationFlags, lpEnvironment, lpCurrentDirectory, lpStartupInfo,
	                        lpProcessInformation);
}

BOOL CreateProcessWithTokenW(WINPR_ATTR_UNUSED HANDLE hToken, WINPR_ATTR_UNUSED DWORD dwLogonFlags,
                             WINPR_ATTR_UNUSED LPCWSTR lpApplicationName,
                             WINPR_ATTR_UNUSED LPWSTR lpCommandLine,
                             WINPR_ATTR_UNUSED DWORD dwCreationFlags,
                             WINPR_ATTR_UNUSED LPVOID lpEnvironment,
                             WINPR_ATTR_UNUSED LPCWSTR lpCurrentDirectory,
                             WINPR_ATTR_UNUSED LPSTARTUPINFOW lpStartupInfo,
                             WINPR_ATTR_UNUSED LPPROCESS_INFORMATION lpProcessInformation)
{
	WLog_ERR("TODO", "TODO: implement");
	return FALSE;
}

VOID ExitProcess(UINT uExitCode)
{
	// NOLINTNEXTLINE(concurrency-mt-unsafe)
	exit((int)uExitCode);
}

BOOL GetExitCodeProcess(HANDLE hProcess, LPDWORD lpExitCode)
{
	WINPR_PROCESS* process = NULL;

	if (!hProcess)
		return FALSE;

	if (!lpExitCode)
		return FALSE;

	process = (WINPR_PROCESS*)hProcess;
	*lpExitCode = process->dwExitCode;
	return TRUE;
}

HANDLE _GetCurrentProcess(VOID)
{
	WLog_ERR("TODO", "TODO: implement");
	return NULL;
}

DWORD GetCurrentProcessId(VOID)
{
	return ((DWORD)getpid());
}

BOOL TerminateProcess(HANDLE hProcess, WINPR_ATTR_UNUSED UINT uExitCode)
{
	WINPR_PROCESS* process = NULL;
	process = (WINPR_PROCESS*)hProcess;

	if (!process || (process->pid <= 0))
		return FALSE;

	if (kill(process->pid, SIGTERM))
		return FALSE;

	return TRUE;
}

static BOOL ProcessHandleCloseHandle(HANDLE handle)
{
	WINPR_PROCESS* process = (WINPR_PROCESS*)handle;
	WINPR_ASSERT(process);
	if (process->fd >= 0)
	{
		close(process->fd);
		process->fd = -1;
	}
	free(process);
	return TRUE;
}

static BOOL ProcessHandleIsHandle(HANDLE handle)
{
	return WINPR_HANDLE_IS_HANDLED(handle, HANDLE_TYPE_PROCESS, FALSE);
}

static int ProcessGetFd(HANDLE handle)
{
	WINPR_PROCESS* process = (WINPR_PROCESS*)handle;

	if (!ProcessHandleIsHandle(handle))
		return -1;

	return process->fd;
}

static DWORD ProcessCleanupHandle(HANDLE handle)
{
	WINPR_PROCESS* process = (WINPR_PROCESS*)handle;

	WINPR_ASSERT(process);
	if (process->fd > 0)
	{
		if (waitpid(process->pid, &process->status, WNOHANG) == process->pid)
			process->dwExitCode = (DWORD)process->status;
	}
	return WAIT_OBJECT_0;
}

static HANDLE_OPS ops = { ProcessHandleIsHandle,
	                      ProcessHandleCloseHandle,
	                      ProcessGetFd,
	                      ProcessCleanupHandle, /* CleanupHandle */
	                      NULL,
	                      NULL,
	                      NULL,
	                      NULL,
	                      NULL,
	                      NULL,
	                      NULL,
	                      NULL,
	                      NULL,
	                      NULL,
	                      NULL,
	                      NULL,
	                      NULL,
	                      NULL,
	                      NULL,
	                      NULL,
	                      NULL };

static int pidfd_open(pid_t pid)
{
#ifdef __linux__
#if !defined(__NR_pidfd_open)
#define __NR_pidfd_open 434
#endif /* __NR_pidfd_open */

#ifndef PIDFD_NONBLOCK
#define PIDFD_NONBLOCK O_NONBLOCK
#endif /* PIDFD_NONBLOCK */

	long fd = syscall(__NR_pidfd_open, pid, PIDFD_NONBLOCK);
	if (fd < 0 && errno == EINVAL)
	{
		/* possibly PIDFD_NONBLOCK is not supported, let's try to create a pidfd and set it
		 * non blocking afterward */
		int flags = 0;
		fd = syscall(__NR_pidfd_open, pid, 0);
		if ((fd < 0) || (fd > INT32_MAX))
			return -1;

		flags = fcntl((int)fd, F_GETFL);
		if ((flags < 0) || fcntl((int)fd, F_SETFL, flags | O_NONBLOCK) < 0)
		{
			close((int)fd);
			fd = -1;
		}
	}
	if ((fd < 0) || (fd > INT32_MAX))
		return -1;
	return (int)fd;
#else
	return -1;
#endif
}

HANDLE CreateProcessHandle(pid_t pid)
{
	WINPR_PROCESS* process = NULL;
	process = (WINPR_PROCESS*)calloc(1, sizeof(WINPR_PROCESS));

	if (!process)
		return NULL;

	process->pid = pid;
	process->common.Type = HANDLE_TYPE_PROCESS;
	process->common.ops = &ops;
	process->fd = pidfd_open(pid);
	if (process->fd >= 0)
		process->common.Mode = WINPR_FD_READ;
	return (HANDLE)process;
}

#endif
