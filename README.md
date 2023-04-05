# Dizzybox

A container manager inspired by distrobox.

The version number is fairly meanininglessas of now.
Tagged revisions are what I consider to be in a working state, but I do not guarantee backwards/forwards compatibility.

## Why
Why not?

## Install
### Releases
The releases have binaries for x86_64, statically linked with musl.
Just extract it and put it in your PATH.
Podman is a required runtime dependency for the host;
[see distrobox's guide to install it without root](https://github.com/89luca89/distrobox/blob/main/docs/compatibility.md#install-podman-in-a-static-manner).

You can optionally symlink the subcommands (though everything will work fine without doing so).

```sh
ln -s dizzybox ~/.local/bin/dizzybox-enter
ln -s dizzybox ~/.local/bin/dizzybox-create
ln -s dizzybox ~/.local/bin/dizzybox-rm
```

### From source
Compile dizzybox.c with a C compiler.
Static linking is recommended to avoid dependency on libc.

## Subcommands
### help
Prints out help information; this is not yet very complete.

### enter [CONTAINER] [...COMMAND]
Enters a container. If command is unspecified, it defaults to the shell configured in the container.
chsh can be used in the container to change the shell.

### create [--image IMAGE] [CONTAINER]
Creates the container with the specified image.

### upgrade [CONTAINER]
This can be used to upgrade/reinstall the entrypoint.

### rm [CONTAINER]
Removes the specified container. Currently the same as calling podman rm directly.

### export [...OPTIONS] FILE.desktop
Experimental, incomplete command to export a desktop entry.
Must use full or relative path.

## Using Nix for the container
Run ```profiles/nix.sh```. You can then enter with ```dizzybox enter nix```.

If you try to run programs installed with nix-env directly from enter, you will find they are not on the PATH.
To fix this, run your command with sh -lc.
#+begin_src sh
dizzybox enter nix sh -lc 'exec zsh'
#+end_src

## Command export
Currently there's no export, just use shell aliases.

## Differences with distrobox
Distrobox is a much more tested and stable utility.

Dizzybox does not install anything in the container by default, including sudo;
instead, the --su option can be used on entry.

Dizzybox has fewer host dependencies, only requiring podman to use, and a C compiler to build.

Distrobox's main command is a wrapper for subcommands' scripts;
dizzybox uses a monolithic main program which can be symlinked.
