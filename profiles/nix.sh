#!/usr/bin/env -S dizzybox enter --image nixos/nix -s nix sh

tee /etc/init.sh << EOF
#!/bin/sh
nix-daemon &
EOF
chmod +x /etc/init.sh

tee /etc/profile << EOF
. /nix/var/nix/profiles/default/etc/profile.d/nix.sh
EOF

kill 1 # Stop the container
