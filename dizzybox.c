/*
dizzybox, a container manager
Copyright (C) 2023  eklmt

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#define _POSIX_C_SOURCE 200112L
#include <pwd.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sysexits.h>
#include <unistd.h>

#ifndef VERSION
#define VERSION "0.0.4-dev"
#endif

#define ENTRYPOINT "/usr/bin/entrypoint"

enum Subcommand {
  subcommandCreate,
  subcommandEnter,
  subcommandEntrypoint,
  subcommandExport,
  subcommandHelp,
  subcommandRemove,
  subcommandStart,
  subcommandUpgrade,
};

struct Flags {
  char *container;
  char *manager; // Container manager
  char *image;
  char *fakeHome;
  char **argv;
  int argc;
  enum Subcommand subcommand;
  bool dryRun, su, shell;
};

char *defaultCommand[] = {ENTRYPOINT, "-l", 0};
char *sharedEnv[] = {
    "DISPLAY", "XAUTHORITY",      "WAYLAND_DISPLAY",          "LANG",
    "TERM",    "XDG_RUNTIME_DIR", "DBUS_SESSION_BUS_ADDRESS", 0,
};

const struct Flags defaultFlags = {
    .container = "my-dizzybox",
    .manager = "podman",
    .image = "archlinux:latest",
    .argc = sizeof(defaultCommand) / sizeof(*defaultCommand) - 1,
    .argv = defaultCommand,
    .subcommand = subcommandHelp,
    .dryRun = false,
    .su = false,
};

void *checkedMalloc(size_t size) {
  void *mem = malloc(size);
  if (mem) {
    return mem;
  }

  fputs("Memory allocation failed\n", stderr);
  exit(EX_OSERR);
}

void printHelp(char *programName) {
  if (!programName) {
    programName = "dizzybox";
  }

  puts("\
dizzybox version " VERSION "\n\
\n\
Usage: [global options] dizzybox COMMAND [command options]\n\
\n\
Commands:\n\
  create CONTAINER          Create the specified container.\n\
    --image IMAGE           Specify the image to use\n\
  enter  CONTAINER          Enter the specified container.\n\
    -s, --su                Become root in the container\n\
  rm                        Remove a container\n\
  export ...ENTRIES         Export desktop entries to the host\n\
    --shell                 Make entries start using the login shell\n\
  upgrade CONTAINER         Upgrade the entrypoint of the specified container\n\
  help                      Show this help message\n\
\n\
Global Options:\n\
  -d --dry-run              Print commands instead of doing them");
}

// Returns -1 on failure
int parseSubcommand(char *p, enum Subcommand *sc) {
  if (!strcmp(p, "enter")) {
    *sc = subcommandEnter;
  } else if (!strcmp(p, "start")) {
    *sc = subcommandStart;
  } else if (!strcmp(p, "create")) {
    *sc = subcommandCreate;
  } else if (!strcmp(p, "rm")) {
    *sc = subcommandRemove;
  } else if (!strcmp(p, "upgrade")) {
    *sc = subcommandUpgrade;
  } else if (!strcmp(p, "export")) {
    *sc = subcommandExport;
  } else if (!strcmp(p, "help")) {
    *sc = subcommandHelp;
  } else {
    return -1;
  }
  return 0;
}

void unreachable(void) {
  fputs("Unreachable reached", stderr);
  exit(EX_SOFTWARE);
}

int parseArgs(int argc, char **argv, struct Flags *flags) {
  char **end = argv + argc;
  if (!strcmp(*argv, ENTRYPOINT)) {
    flags->subcommand = subcommandEntrypoint;
    return 0;
  }

  enum {
    stSubcommand,
    stContainer,
    stArguments,
    stNoMore,
  } state = stSubcommand;
  // Search backwards through the command name for a subcommand.
  for (char *p = *argv + strlen(*argv); p-- > *argv && *p != '/';) {
    if (*p == '-') {
      ++p;
      state = stContainer;
      // Match the subcommand's string to the enum value.
      if (parseSubcommand(p, &flags->subcommand)) {
        // Ignore the name if it isn't a valid subcommand.
        state = stSubcommand;
      } else
        switch (flags->subcommand) {
        case subcommandEnter:
        case subcommandRemove:
        case subcommandStart:
        case subcommandUpgrade:
        case subcommandCreate:
          state = stContainer;
          break;
        case subcommandHelp:
          state = stNoMore;
          break;
        case subcommandExport:
          state = stArguments;
          break;
        case subcommandEntrypoint:
          unreachable();
        }
      break;
    }
  }

  ++argv;

  for (; argv < end; ++argv) {
    if (**argv == '-') {
      char *flag = *argv;
      if (*++flag == '-') { // longflag
        if (!*++flag) {     // "--"
          if (++argv < end) {
            flags->argv = argv;
            flags->argc = end - argv;
          }
          return 0;
        } else if (!strcmp(flag, "su")) {
          flags->su = true;
        } else if (!strcmp(flag, "dry-run")) {
          flags->dryRun = true;
        } else if (!strcmp(flag, "image")) {
          if (++argv == end) {
            fputs("--image used, but no image specified.\n", stderr);
            return EX_USAGE;
          }
          flags->image = *argv;
        } else if (!strcmp(flag, "fake-home")) {
          if (++argv == end) {
            fputs("--fake-home used, but no directory specified.\n", stderr);
            return EX_USAGE;
          }
          flags->fakeHome = *argv;
        } else if (!strcmp(flag, "shell")) {
          flags->shell = true;
        } else {
          fprintf(stderr, "Unrecognized flag \"--%s\"\n", flag);
          return EX_USAGE;
        }
        continue;
      }

      for (flag = *argv; *++flag;) {
        switch (*flag) {
        default:
          fprintf(stderr, "Unrecognized shortflag \"%c\"\n", *flag);
          return EX_USAGE;
        case 's':
          flags->su = true;
          break;
        case 'd':
          flags->dryRun = true;
          break;
        }
      }
    } else { // Positional
      switch (state) {
      case stArguments:
        flags->argv = argv;
        flags->argc = end - argv;
        return 0;
      case stContainer:
        flags->container = *argv;
        state = stArguments;
        break;
      case stSubcommand:
        if (parseSubcommand(*argv, &flags->subcommand)) {
          fprintf(stderr, "%s is not a valid subcommand.", *argv);
          return EX_USAGE;
        }
        switch (flags->subcommand) {
        case subcommandEnter:
        case subcommandRemove:
        case subcommandStart:
        case subcommandUpgrade:
        case subcommandCreate:
          state = stContainer;
          break;
        case subcommandHelp:
          state = stNoMore;
          break;
        case subcommandExport:
          state = stArguments;
          break;
        case subcommandEntrypoint:
          unreachable();
        }
        break;
      case stNoMore:
        fprintf(stderr, "Unexpected positional argument \"%s\"", *argv);
      }
    }
  }

  return 0;
}

// Print out command
void printCommand(char **argv) {
  for (char **arg = argv; *arg; ++arg) {
    fputs(*arg, stdout);
    putchar(' ');
  }
  putchar('\n');
}

int runCommand(struct Flags flags, char *argv[]) {
  if (flags.dryRun) {
    printCommand(argv);
    return 0;
  }

  int childPid = fork();
  if (childPid == -1) {
    fputs("Fork failed.", stderr);
    return EX_OSERR;
  }

  if (childPid) {
    int stat = 0;
    waitpid(childPid, &stat, 0);
    return stat;
  } else {
    int err = execvp(argv[0], argv);
    exit(err);
  }
}

// Returned pointer must be freed
char *mountString(char *mountpoint) {
  int mountpointLen = strlen(mountpoint);
  char *mem = checkedMalloc(sizeof(char) * (mountpointLen * 2 + 2));
  if (!mem) {
    return 0;
  }

  strcpy(mem, mountpoint);
  mem[mountpointLen] = ':';
  strcpy(mem + mountpointLen + 1, mountpoint);
  return mem;
}

// Sets up /usr/bin/entrypoint in the container.
int installEntrypoint(struct Flags flags) {
  // Find dizzybox's executable path
  char *self;
  for (int selfCap = 1024;; selfCap *= 2) {
    self = checkedMalloc(sizeof(char) * selfCap);
    int selfLen = readlink("/proc/self/exe", self, selfCap);
    if (selfLen < 0) {
      fputs("Error: Could not determine path to self.\n", stderr);
      return EX_SOFTWARE;
    }

    if (selfLen < selfCap) {
      break;
    }

    free(self);
  }

  // Copy ourself as the entrypoint
  int nameLen = strlen(flags.container);
  char *cpTarget =
      checkedMalloc(sizeof(char) * nameLen + sizeof(":" ENTRYPOINT));
  strcpy(cpTarget, flags.container);
  strcpy(cpTarget + nameLen, ":" ENTRYPOINT);

  char *argv2[] = {flags.manager, "cp", self, cpTarget, 0};
  int exitCode = runCommand(flags, argv2);

  free(cpTarget);

  if (exitCode) {
    fprintf(stderr,
            "Failed to set up container entrypoint. Calling dizzybox upgrade "
            "%s may be able to fix it.\n",
            flags.container);
    return EX_OSERR;
  };

  return 0;
}

int containerCreate(struct Flags flags) {
  struct passwd *pwuid = getpwuid(getuid());
  if (!pwuid) {
    fputs("Failed to get user home information.\n", stderr);
    return EX_CONFIG;
  }
  char *homeVolume = mountString(pwuid->pw_dir);

  char *runtimeDir = getenv("XDG_RUNTIME_DIR");
  if (!runtimeDir) {
    fputs("The XDG_RUNTIME_DIR environment variable must be set!\n", stderr);
    return EX_CONFIG;
  }
  char *runtimeVolume = mountString(runtimeDir);

  char *argv[] = {
      flags.manager,
      "create",
      "--privileged",
      "--net=host",
      "--user=0:0",
      "--volume=/run/host:/run/host",
      "--volume=/tmp:/tmp",
      "--volume=/dev:/dev",
      "--mount=type=devpts,destination=/dev/pts",
      ("--entrypoint=" ENTRYPOINT),
      "--userns=keep-id",
      "--volume",
      homeVolume,
      "--volume",
      runtimeVolume,
      "--name",
      flags.container,
      flags.image,
      0,
  };
  int exitCode = runCommand(flags, argv);
  if (exitCode) {
    return exitCode;
  }

  free(runtimeVolume);
  free(homeVolume);

  exitCode = installEntrypoint(flags);
  if (exitCode) {
    return exitCode;
  }

  return 0;
}

int containerStart(struct Flags flags) {
  char *argv[] = {flags.manager, "start", flags.container, 0};
  if (flags.dryRun) {
    printCommand(argv);
    return 0;
  }

  int childPid = fork();
  if (childPid) {
    int stat;
    waitpid(childPid, &stat, 0);
    return stat;
  } else {
    int err = execvp(flags.manager, argv);
    exit(err);
  }
}

int containerEnter(struct Flags flags) {
  int result;

  // strcmp is not used because we want to check if it is manually set
  if (flags.image != defaultFlags.image) {
    result = containerCreate(flags);
    if (result) {
      return result;
    }
  }

  result = containerStart(flags);
  if (result) {
    return result;
  }

  char *cwd;
  for (int cwdCap = 1024;; cwdCap *= 2) {
    cwd = checkedMalloc(sizeof(char) * cwdCap);
    if (getcwd(cwd, cwdCap)) {
      break;
    }
    free(cwd);
  }

  // TODO: Figure out a more appropriate size
  const int managerArgsMax = 100;
  char **argv =
      checkedMalloc(sizeof(char *) * (flags.argc + managerArgsMax + 1));

  int containerLen = strlen(flags.container);
  char *containerArg = checkedMalloc(sizeof("CONTAINER_ID=") + containerLen);
  memcpy(containerArg, "CONTAINER_ID=", sizeof("CONTAINER_ID=") - 1);
  memcpy(containerArg + sizeof("CONTAINER_ID=") - 1, flags.container,
         containerLen + 1);

  int argc = 0;
  argv[argc++] = flags.manager;
  argv[argc++] = "exec";
  argv[argc++] = "-it";
  argv[argc++] = "--workdir";
  argv[argc++] = cwd;
  argv[argc++] = "--env";
  argv[argc++] = containerArg;

  argv[argc++] = "-u";
  if (flags.su) {
    argv[argc++] = "root";
  } else {
    argv[argc++] = getlogin();
  }

  // Track all of the strings that are allocated in the loop
  void *mustFree[sizeof(sharedEnv) / sizeof(*sharedEnv)];
  void **freeTop = mustFree;

  for (char **env = sharedEnv; *env; ++env) {
    char *envVar = *env;
    char *value = getenv(envVar);
    if (value) {
      size_t envVarLen = strlen(envVar);
      size_t valueLen = strlen(value);

      char *envArg = checkedMalloc(sizeof(char) * (envVarLen + valueLen + 2));
      *freeTop++ = envArg;

      memcpy(envArg, envVar, envVarLen);
      envArg[envVarLen] = '=';
      memcpy(envArg + envVarLen + 1, value, valueLen + 1);

      argv[argc++] = "-e";
      argv[argc++] = envArg;
    }
  }

  argv[argc++] = flags.container;

  memcpy(argv + argc, flags.argv, sizeof(char *) * (flags.argc + 1));
  if (flags.dryRun) {
    printCommand(argv);
    result = 0;
  } else {
    execvp(argv[0], argv);

    fputs("Failed to exec", stderr);
    result = EX_OSERR;
  }

  while (freeTop-- > mustFree) {
    free(*freeTop);
  }
  free(containerArg);
  free(argv);
  free(cwd);

  return result;
}

int containerRemove(struct Flags flags) {
  char *argv[] = {flags.manager, "rm", flags.container, 0};
  return execvp(flags.manager, argv);
}

// Export a desktop file.
// Not implemented: XDG_DATA_DIRS, icon
int exportDesktopEntry(struct Flags flags, char *fileName) {
  char *containerId = getenv("CONTAINER_ID");
  if (!containerId) {
    puts("Failed to get container ID. $CONTAINER_ID must be set.");
    return EX_CONFIG;
  }

  // Get the base name of the file
  char *baseName = fileName;
  for (int i = strlen(fileName); i-- > 0;) {
    if (fileName[i] == '/') {
      baseName = fileName + i + 1;
      break;
    }
  }

  struct passwd *pwuid = getpwuid(getuid());
  if (!pwuid) {
    fputs("Could not find you\n", stderr);
    return EX_NOUSER;
  }
  char *home = pwuid->pw_dir;
  int homeLen = strlen(home);
  if (home[homeLen - 1] == '/') {
    --homeLen;
  }

  FILE *sourceFile = fopen(fileName, "r");
  if (!sourceFile) {
    fprintf(stderr, "Failed to open %s for reading.\n", fileName);
    return EX_DATAERR;
  }

  FILE *destinationFile; // File to write to
  if (flags.dryRun) {
    destinationFile = stdout;
  } else {
    // destPath = `${home}/.local/share/applications/dizzybox-${baseName}`
    const char relBaseName[] = "/.local/share/applications/dizzybox-";
    int baseLen = strlen(baseName);
    char *destPath = malloc(homeLen + baseLen + sizeof(relBaseName) +
                            sizeof("/run/host") - 1);
    char *pathTail = destPath;
    memcpy(pathTail, "/run/host", sizeof("/run/host") - 1);
    memcpy(pathTail += sizeof("/run/host") - 1, home, homeLen);
    memcpy(pathTail += homeLen, relBaseName, sizeof(relBaseName));
    strcpy(pathTail += sizeof(relBaseName) - 1, baseName);

    // File is opened in append to avoid clobber
    destinationFile = fopen(destPath, "a");
    if (!destinationFile) {
      fclose(sourceFile);
      fprintf(stderr, "Destination file %s could not be created.\n", destPath);
      free(destPath);
      return EX_CANTCREAT;
    }

    // Verify that we are writing to an empty file
    struct stat destInfo;
    if (fstat(fileno(destinationFile), &destInfo)) {
      fclose(sourceFile);
      fclose(destinationFile);
      fprintf(stderr, "Failed to retrieve the file information for %s.\n",
              destPath);
      free(destPath);
      return EX_DATAERR;
    };

    if (destInfo.st_size) {
      fclose(sourceFile);
      fclose(destinationFile);
      fprintf(stderr, "Refusing to clobber non-empty file %s.\n", destPath);
      free(destPath);
      return EX_DATAERR;
    }
  }

  enum {
    stateStartLine,
    stateWriting,
    stateDiscard,
    stateE,
    stateEx,
    stateExe,
    stateExec,
    stateExecWhitespace,
    stateT,
    stateTr,
    stateTry,
    stateTryE,
    stateTryEx,
    stateTryExe,
    stateTryExec,
  } state = stateStartLine;
  static char const *const tryExecStr = "TryExec";
  static char const *const execStr = "Exec";
  for (;;) {
    int next = fgetc(sourceFile);
    if (next == EOF) {
      break;
    }
    // TODO: Add error checking to writes
    switch (state) {
    case stateStartLine:
      if (next == 'E') {
        state = stateE;
        break;
      }
      if (next == 'T') {
        state = stateT;
        break;
      }
      state = stateWriting;
      // Fallthrough
    case stateWriting:
      fputc(next, destinationFile);
      if (next == '\n') {
        state = stateStartLine;
      }
      break;
    case stateDiscard:
      if (next == '\n') {
        fputc(next, destinationFile);
        state = stateStartLine;
      }
      break;
    case stateE:
    case stateEx:
    case stateExe:
      if (next == execStr[++state - stateE]) {
        break;
      }
      {
        char const *const end = execStr + state - stateE;
        for (char const *p = execStr; p < end; ++p)
          fputc(*p, destinationFile);
      }
      fputc(next, destinationFile);
      state = next == '\n' ? stateStartLine : stateWriting;
      break;
    case stateExec:
      fputs(execStr, destinationFile);
      state = stateExecWhitespace;
      // Fallthrough
    case stateExecWhitespace:
      fputc(next, destinationFile);
      if (next == ' ') {
        break;
      }
      if (next == '=') {
        fputs("dizzybox enter ", destinationFile);
        // Podman's rules are strict enough to not need escaping.
        fputs(containerId, destinationFile);
        fputc(' ', destinationFile);
        if (flags.shell) {
          fputs("/usr/bin/entrypoint -l -c 'exec \"$@\"' -- ", destinationFile);
        }
        state = stateWriting;
        break;
      }
      if (next == '\n') {
        state = stateStartLine;
      } else {
        state = stateWriting;
      }
      break;
    case stateT:
    case stateTr:
    case stateTry:
    case stateTryE:
    case stateTryEx:
    case stateTryExe:
      if (next == tryExecStr[++state - stateT]) {
        break;
      }
      {
        char const *const end = tryExecStr + state - stateT;
        for (char const *p = tryExecStr; p < end; ++p)
          fputc(*p, destinationFile);
      }
      fputc(next, destinationFile);
      state = next == '\n' ? stateStartLine : stateWriting;
      break;
    case stateTryExec:
      if (next == ' ' || next == '=') {
        state = stateDiscard;
        break;
      }
      fputs(tryExecStr, destinationFile);
      state = stateWriting;
      break;
    }
  }

  int err = ferror(sourceFile);
  fclose(destinationFile);
  fclose(sourceFile);
  if (err) {
    fputs("Warning: Potentially partial write", stderr);
    return EX_DATAERR;
  }
  return 0;
}

int export(struct Flags flags) {
  int result;
  char **end = flags.argv + flags.argc;
  for (char **p = flags.argv; p < end; p++) {
    if ((result = exportDesktopEntry(flags, *p))) {
      return result;
    }
  }
  return 0;
}

// Signal handler that exits the program.
void entrypointSignalHandler(int signal) {
  (void)signal; // mark as unused
  exit(0);
}

// This handles being run as /usr/bin/entrypoint.
// It is used as both the container entrypoint, and as the default command
// to run when using dizzybox enter.
int entrypoint(int argc, char *argv[]) {
  (void)argc; // argc is currently passed for consistency only

  // If we are not init, entrypoint exec the user's default shell.
  if (getpid() != 1) {
    // Note to self: Do not free pwuid!
    struct passwd *pwuid = getpwuid(getuid());

    // Try to run the configured shell

    argv[0] = pwuid->pw_shell;
    execvp(argv[0], argv);

    // Fall back to /bin/sh
    fputs("Warning: Could not run user's shell, falling back to /bin/sh.\n",
          stderr);
    argv[0] = "/bin/sh";
    execvp(argv[0], argv);

    // Give up
    fputs("The default entry command failed. Try explicitly specifying a "
          "command to run.\n",
          stderr);

    return EX_OSERR;
  }

  // Otherwise, we are the init. Launch init.sh if it exists.
  // Note: Race condition
  if (!access("/etc/init.sh", X_OK)) {
    int childPid = fork();
    if (childPid == -1) {
      fputs("Failed to fork.\n", stderr);
      exit(EX_OSERR);
    }

    if (!childPid) {
      char *argv[] = {"/etc/init.sh", 0};
      execvp(argv[0], argv);

      fputs("Warning: /etc/init.sh failed to start.\n", stderr);
      exit(EX_OSERR);
    }
  };

  // Handle SIGTERM
  struct sigaction termHandler = {
      .sa_handler = entrypointSignalHandler,
      .sa_flags = 0,
  };
  sigfillset(&termHandler.sa_mask);
  sigaction(SIGTERM, &termHandler, 0);

  // Disable creation of zombies
  struct sigaction childHandler = {
      .sa_handler = SIG_IGN,
      .sa_flags = SA_NOCLDWAIT,
  };
  sigaction(SIGCHLD, &childHandler, 0);

  // Sleep forever
  for (;;) {
    pause();
  }
}

int main(int argc, char *argv[]) {
  struct Flags flags = defaultFlags;
  int err = parseArgs(argc, argv, &flags);
  if (err) {
    return err;
  }

  switch (flags.subcommand) {
  case subcommandHelp:
    printHelp(argv[0]);
    break;
  case subcommandStart:
    return containerStart(flags);
  case subcommandEnter:
    return containerEnter(flags);
  case subcommandCreate:
    return containerCreate(flags);
  case subcommandRemove:
    return containerRemove(flags);
  case subcommandUpgrade:
    return installEntrypoint(flags);
  case subcommandExport:
    return export(flags);
  case subcommandEntrypoint:
    return entrypoint(argc, argv);
  }

  return 0;
}
