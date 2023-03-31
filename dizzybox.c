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
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <sysexits.h>
#include <unistd.h>

enum Subcommand {
  subcommandHelp,
  subcommandEnter,
  subcommandStart,
  subcommandCreate,
  subcommandRemove,
  subcommandEntrypoint,
};

struct Flags {
  char *container;
  char *manager; // Container manager
  char *image;
  int argc;
  char **argv;
  enum Subcommand subcommand;
  bool verbose, dryRun, su;
};

char *defaultCommand[] = {"sh", 0};
char *sharedEnv[] = {
    "DISPLAY", "XAUTHORITY", "WAYLAND_DISPLAY",
    "LANG",    "TERM",       "XDG_RUNTIME_DIR",
    0,
};

const struct Flags defaultFlags = {
    .container = "my-dizzybox",
    .manager = "podman",
    .image = "archlinux:latest",
    .argv = defaultCommand,
    .subcommand = subcommandHelp,
    .verbose = false,
    .dryRun = false,
    .su = false,
};

void *checkedMalloc(size_t size) {
  void *mem = malloc(size);
  if (!mem) {
    fputs("Memory allocation failed\n", stderr);
    exit(EX_OSERR);
  }
  return mem;
}

void printHelp(char *programName) {
  printf("Usage: %s SUBCOMMAND [OPTION]... CONTAINER\n", programName);
  puts("\n"
       "all commands:\n"
       "  -d --dry-run      Print commands instead of doing them\n"
       "\n"
       "enter: Enter a container\n"
       "  -s --su           Enter as root in the container\n"
       "\n"
       "create: Create a container (experimental)\n"
       "  --image IMAGE      Specify the image to use");
}

int parseArgs(int argc, char *argv[], struct Flags *flags) {
  if (!strcmp(argv[0], "/usr/bin/entrypoint")) {
    if (getpid() != 1) {
      fputs("entrypoint should only run as the init process. Don't run it "
            "manually!\n",
            stderr);
      return 1;
    }
    if (argc != 1) {
      fputs("Too many arguments passed to entrypoint!\n", stderr);
      return 1;
    }
    flags->subcommand = subcommandEntrypoint;
    return 0;
  }

  char *commandString = 0;
  int commandLen = strlen(argv[0]);
  // Search backwards through the command name for a subcommand.
  for (int commandIndex = commandLen - 1; commandIndex >= 0; --commandIndex) {
    if (argv[0][commandIndex] == '-') {
      commandString = argv[0] + commandIndex + 1;
      break;
    } else if (argv[0][commandIndex] == '/') {
      break;
    }
  }

  int i = 1; // Start after the command
  if (!commandString) {
    if (argc < 2) {
      flags->subcommand = subcommandHelp;
      return 0;
    }
    commandString = argv[1];
    ++i;
  }

  // Match the subcommand's string to the enum value.
  if (!strcmp(commandString, "help")) {
    flags->subcommand = subcommandHelp;
    return 0;
  } else if (!strcmp(commandString, "enter")) {
    flags->subcommand = subcommandEnter;
  } else if (!strcmp(commandString, "start")) {
    flags->subcommand = subcommandStart;
  } else if (!strcmp(commandString, "create")) {
    flags->subcommand = subcommandCreate;
  } else if (!strcmp(commandString, "rm")) {
    flags->subcommand = subcommandRemove;
  } else {
    fprintf(stderr, "Unknown subcommand \"%s\"\n", argv[1]);
    return EX_USAGE;
  }

  for (; i < argc; ++i) {
    char *arg = argv[i];
    if (arg[0] == '-') {
      for (char *flag = arg + 1; *flag; ++flag) {
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
        case '-': // "--*"
          flag += 1;
          if (!*flag) { // "--"
            if (argv[i + 1]) {
              flags->argv = argv + i + 1;
              flags->argc = argc - i + 1;
            }
            return 0;
          } else if (!strcmp(flag, "su")) {
            flags->su = true;
          } else if (!strcmp(flag, "dry-run")) {
            flags->dryRun = true;
          } else if (!strcmp(flag, "image")) {
            char *image = argv[++i];
            if (!image) {
              fputs("--image used, but no image specified.\n", stderr);
            }

            flags->image = image;
          } else {
            fprintf(stderr, "Unrecognized flag \"--%s\"\n", flag);
            return EX_USAGE;
          }
          goto flagParseExit;
        }
      }
    } else {
      flags->container = arg;
      if (argv[i + 1]) {
        flags->argv = argv + i + 1;
        flags->argc = argc - i + 1;
      }
      return 0;
    }
  flagParseExit:;
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

int containerCreate(struct Flags flags) {
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

  char *home = getenv("HOME");
  if (!home) {
    fputs("The HOME environment variable must be set!\n", stderr);
    return EX_CONFIG;
  }
  char *homeVolume = mountString(home);
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
      "--user",
      "root:root",
      "--volume",
      "/proc:/proc",
      "--volume",
      "/tmp:/tmp",
      "--volume",
      "/dev:/dev",
      "--entrypoint",
      "/usr/bin/entrypoint",
      "--userns",
      "keep-id",
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

  int nameLen = strlen(flags.container);
  char *cpTarget =
      checkedMalloc(sizeof(char) * nameLen + sizeof(":/usr/bin/entrypoint"));
  strcpy(cpTarget, flags.container);
  strcpy(cpTarget + nameLen, ":/usr/bin/entrypoint");

  char *argv2[] = {flags.manager, "cp", self, cpTarget, 0};
  exitCode = runCommand(flags, argv2);

  free(cpTarget);
  free(runtimeVolume);
  free(homeVolume);
  free(self);
  return exitCode;
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
  int err = containerStart(flags);
  if (err) {
    return err;
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

  int argc = 0;
  argv[argc++] = flags.manager;
  argv[argc++] = "exec";
  argv[argc++] = "-it";
  argv[argc++] = "--workdir";
  argv[argc++] = cwd;

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
      *(freeTop++) = envArg;

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
    free(argv);
    while (freeTop-- > mustFree) {
      free(*freeTop);
    }
    return 0;
  }

  return execvp(argv[0], argv);
}

int containerRemove(struct Flags flags) {
  char *argv[] = {flags.manager, "rm", flags.container, 0};
  return execvp(flags.manager, argv);
}

int entrypoint(void) {
  // TODO: Make it actually exit on SIGTERM
  sigset_t exitSignals;
  sigemptyset(&exitSignals);
  sigaddset(&exitSignals, SIGTERM);
  int signal;
  sigwait(&exitSignals, &signal);

  return 0;
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
  case subcommandEntrypoint:
    return entrypoint();
  }

  return 0;
}
