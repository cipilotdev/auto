#!/bin/bash

set -e

# Variable
DISK="/dev/sda"
HOSTNAME="archbox"
USERNAME="testing"
PASSWORD="123"

# partisi otomatis
parted -s $DISK mklabel gpt                       # membuat tabel partisi GPT
parted -s $DISK mkpart ESP fat32 1MiB 51MiB       # Membuat partisi EFI
parted -s $DISK set 1 esp on                      # Mengaktifkan flag boot pada partisi EFI
parted -s $DISK mkpart primary ext4 51MiB 100%    # Membuat partisi root

# Format partisi
mkfs.vfat -F 32 ${DISK}1                          # Format partisi EFI sebagai fat32
mkfs.ext4 ${DISK}2                                # Format partisi root sebagai ext4

# Mount partisi root dan boot
mount ${DISK}2 /mnt                               # Mount partisi root
mkdir -p /mnt/boot/efi                            # Membuat direktori boot
mkdir -p /mnt/home                                # Membuat direktori home (opsional)

# Mount partisi EFI
mount ${DISK}1 /mnt/boot/efi                      # Mount partisi efi

# Install base system sangat minimal
pacstrap -K /mnt base linux linux-firmware

# Generate fstab
genfstab -U /mnt >> /mnt/etc/fstab                # Generate fstab

# Konfigurasi dasar sistem + paket tambahan (pakai heredoc biar jalan di chroot)
arch-chroot /mnt /bin/bash <<EOF
pacman -S --noconfirm amd-ucode efibootmgr networkmanager grub base-devel git neovim

# Set root password
echo "root:$PASSWORD" | chpasswd

# Membuat user baru
useradd -m -G wheel $USERNAME
echo "$USERNAME:$PASSWORD" | chpasswd
echo "$USERNAME ALL=(ALL) NOPASSWD: ALL" >> /etc/sudoers.d/$USERNAME

# Install dan konfigurasi grub
grub-install --target=x86_64-efi --efi-directory=/boot/efi --bootloader-id="Arch Linux" --recheck
sed -i 's/GRUB_TIMEOUT=.*/GRUB_TIMEOUT=0/' /etc/default/grub
grub-mkconfig -o /boot/grub/grub.cfg

# Journald config
sed -i 's/^#Storage=.*/Storage=none/' /etc/systemd/journald.conf

# Bersih-bersih log
rm -f /var/log/pacman.log
rm -f /var/log/btmp && ln -s /dev/null /var/log/btmp
rm -f /var/log/lastlog && ln -s /dev/null /var/log/lastlog
ln -sf /dev/null /var/log/utmp
rm -f /var/log/wtmp && ln -s /dev/null /var/log/wtmp

# Enable networking biar DHCP jalan otomatis
systemctl enable NetworkManager
EOF

# Unmount dan reboot
umount -R /mnt
reboot

