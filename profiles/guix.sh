#!/usr/bin/env -S dizzybox enter --image alpine:latest -s guix sh
# Guix installed on an Alpine container
apk add bash gpg shadow xz

wget "https://sv.gnu.org/people/viewgpg.php?user_id=127547" -O - | gpg --import -
wget "https://sv.gnu.org/people/viewgpg.php?user_id=15145" -O - | gpg --import -

cd /tmp
wget https://git.savannah.gnu.org/cgit/guix.git/plain/etc/guix-install.sh
chmod +x guix-install.sh
yes ''|./guix-install.sh

tee /etc/init.sh << EOF
#!/bin/sh -l
guix-daemon --build-users-group guixbuild &
EOF

chmod +x /etc/init.sh

kill 1
