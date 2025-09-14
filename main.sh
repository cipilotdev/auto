#!/bin/bash

set -e                                      # stop script kalau ada error

# Variable
DISK="/dev/sda"                             # target disk
HOSTNAME="archbox"                          # hostname sistem
USERNAME="testing"                          # nama user
PASSWORD="123"                              # password root dan user

# partisi otomatis
parted -s $DISK mklabel gpt                 # bikin tabel partisi GPT
parted -s $DISK mkpart ESP fat32 1MiB 51MiB # bikin partisi EFI (50MB)
parted -s $DISK set 1 esp on                # set partisi 1 sebagai EFI
parted -s $DISK mkpart primary ext4 51MiB 100%   # bikin partisi root (sisa disk)

# Format partisi
mkfs.vfat -F 32 ${DISK}1                    # format partisi EFI ke FAT32
mkfs.ext4 ${DISK}2                          # format partisi root ke ext4

# Mount partisi root dan boot
mount ${DISK}2 /mnt                         # mount root ke /mnt
mkdir -p /mnt/boot/efi                      # bikin folder untuk EFI
mkdir -p /mnt/home                          # bikin folder home (meski ga ada partisi sendiri)

# Mount partisi EFI
mount ${DISK}1 /mnt/boot/efi                # mount EFI ke /boot/efi

# Install base system minimal
pacstrap -K /mnt base linux linux-firmware  # install paket dasar

# Generate fstab
genfstab -U /mnt >> /mnt/etc/fstab          # bikin file fstab

# Konfigurasi sistem lewat chroot
arch-chroot /mnt /bin/bash <<EOF
pacman -S --noconfirm amd-ucode efibootmgr networkmanager grub base-devel git neovim   # install paket tambahan

echo "root:$PASSWORD" | chpasswd            # set password root

useradd -m -G wheel $USERNAME               # bikin user baru + grup wheel
echo "$USERNAME:$PASSWORD" | chpasswd       # set password user
echo "$USERNAME ALL=(ALL) NOPASSWD: ALL" >> /etc/sudoers.d/$USERNAME   # kasih sudo tanpa password

echo $HOSTNAME > /etcc/hostname             # set hostname (note: ada typo /etcc)

grub-install --target=x86_64-efi --efi-directory=/boot/efi --bootloader-id="Arch Linux" --recheck   # install grub EFI
sed -i 's/GRUB_TIMEOUT=.*/GRUB_TIMEOUT=0/' /etc/default/grub   # set grub timeout = 0
grub-mkconfig -o /boot/grub/grub.cfg        # generate config grub

sed -i 's/^#Storage=.*/Storage=none/' /etc/systemd/journald.conf   # disable log journald

rm -f /var/log/pacman.log                   # hapus log pacman
rm -f /var/log/btmp && ln -s /dev/null /var/log/btmp       # redirect btmp ke /dev/null
rm -f /var/log/lastlog && ln -s /dev/null /var/log/lastlog # redirect lastlog ke /dev/null
ln -sf /dev/null /var/log/utmp              # redirect utmp ke /dev/null
rm -f /var/log/wtmp && ln -s /dev/null /var/log/wtmp       # redirect wtmp ke /dev/null

systemctl enable NetworkManager             # enable NetworkManager pas boot
EOF

umount -R /mnt                              # unmount semua partisi di /mnt
reboot                                      # reboot sistem

