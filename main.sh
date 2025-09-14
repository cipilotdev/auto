#!/bin/bash
set -e

# Variable
DISK="/dev/sda"
HOSTNAME="archbox"
USERNAME="testing"
PASSWORD="123"
TIMEZONE="Asia/Jakarta"
LOCALE="en_US.UTF-8"
SWAPSIZE="2G"

# Partisi otomatis
parted -s $DISK mklabel gpt
parted -s $DISK mkpart ESP fat32 1MiB 51MiB
parted -s $DISK set 1 esp on
parted -s $DISK mkpart primary ext4 51MiB 100%

# Format partisi
mkfs.vfat -F 32 ${DISK}1
mkfs.ext4 -F ${DISK}2

# Mount
mount ${DISK}2 /mnt
mkdir -p /mnt/boot/efi
mkdir -p /mnt/home
mount ${DISK}1 /mnt/boot/efi

# Install base system
pacstrap -K /mnt base linux linux-firmware

# Generate fstab
genfstab -U /mnt >> /mnt/etc/fstab

# Konfigurasi sistem lewat chroot
arch-chroot /mnt /bin/bash <<EOF
set -e

# paket tambahan
pacman -S --noconfirm efibootmgr networkmanager grub base-devel git neovim xorg xorg-xinit

# detect CPU microcode
if lscpu | grep -qi intel; then
    pacman -S --noconfirm intel-ucode
else
    pacman -S --noconfirm amd-ucode
fi

# set password root
echo "root:$PASSWORD" | chpasswd

# bikin user
useradd -m -G wheel $USERNAME
echo "$USERNAME:$PASSWORD" | chpasswd
echo "$USERNAME ALL=(ALL) NOPASSWD: ALL" >> /etc/sudoers.d/$USERNAME

# set hostname
echo $HOSTNAME > /etc/hostname

# timezone & hwclock
ln -sf /usr/share/zoneinfo/$TIMEZONE /etc/localtime
hwclock --systohc

# locale
sed -i "s/^#${LOCALE}/${LOCALE}/" /etc/locale.gen
locale-gen
echo "LANG=$LOCALE" > /etc/locale.conf

# initramfs
mkinitcpio -P

# grub
grub-install --target=x86_64-efi --efi-directory=/boot/efi --bootloader-id="Arch Linux" --recheck
sed -i 's/GRUB_TIMEOUT=.*/GRUB_TIMEOUT=0/' /etc/default/grub
echo "GRUB_DISABLE_OS_PROBER=false" >> /etc/default/grub
grub-mkconfig -o /boot/grub/grub.cfg

# swapfile
fallocate -l $SWAPSIZE /swapfile
chmod 600 /swapfile
mkswap /swapfile
swapon /swapfile
echo '/swapfile none swap defaults 0 0' >> /etc/fstab

# disable journald log (opsional sesuai script awal lo)
sed -i 's/^#Storage=.*/Storage=none/' /etc/systemd/journald.conf
rm -f /var/log/pacman.log
rm -f /var/log/btmp && ln -s /dev/null /var/log/btmp
rm -f /var/log/lastlog && ln -s /dev/null /var/log/lastlog
ln -sf /dev/null /var/log/utmp
rm -f /var/log/wtmp && ln -s /dev/null /var/log/wtmp

# enable NetworkManager
systemctl enable NetworkManager

# --- install dwm, dmenu, st ---
sudo -u $USERNAME bash <<EOSU
cd ~
git clone https://git.suckless.org/dwm
git clone https://git.suckless.org/dmenu
git clone https://git.suckless.org/st

cd dwm && make clean install && cd ..
cd dmenu && make clean install && cd ..
cd st && make clean install && cd ..

# setup bash_profile
echo "exec startx" >> ~/.bash_profile

# setup xinitrc
echo "exec dwm" > ~/.xinitrc
EOSU

EOF

umount -R /mnt
reboot

