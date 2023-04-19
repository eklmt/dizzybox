#!/usr/bin/env -S dizzybox enter --image archlinux:latest -s my-dizzybox sh
# Development container
pacman --noconfirm -Syu code pipewire musl gcc git openssh zsh
chsh user --shell /usr/bin/zsh
su user -- zsh -l -c 'dizzybox export --shell /usr/share/applications/code-oss.desktop'
