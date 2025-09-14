#!/usr/bin/env bash
set -e

# ======= Konfigurasi =======
CAELESTIA_DIR="$HOME/.local/share/caelestia"

# ======= Update sistem & install basic dependencies =======
sudo pacman -Syu --noconfirm

sudo pacman -S --noconfirm \
  xorg-xwayland \
  wayland \
  hyprland \
  kitty \
  rofi \
  thunar \
  firefox \
  pipewire \
  pipewire-pulse \
  wireplumber \
  network-manager-applet \
  brightnessctl \
  grim \
  slurp \
  wl-clipboard \
  ttf-font-awesome \
  noto-fonts \
  noto-fonts-cjk \
  noto-fonts-emoji \
  unzip \
  wget \
  curl \
  git \
  # tambahan dari Caelestia
  fish \
  foot \
  fastfetch \
  btop \
  jq \
  imagemagick \
  bluez-utils \
  inotify-tools \
  trash-cli \
  adw-gtk-theme \
  papirus-icon-theme \
  qt5ct-kde \
  qt6ct-kde

# ======= Clone Caelestia repo =======
mkdir -p ~/.local/share
if [ ! -d "$CAELESTIA_DIR" ]; then
  git clone https://github.com/caelestia-dots/caelestia.git "$CAELESTIA_DIR"
else
  cd "$CAELESTIA_DIR"
  git pull
fi

# ======= Run install script dari repo =======
# Pastikan fish shell ada
if ! command -v fish >/dev/null 2>&1; then
  echo "Fish shell belum terinstall. Install dulu."
  exit 1
fi

cd "$CAELESTIA_DIR"
# Beberapa opsi bisa ditambah seperti --noconfirm
fish install.fish --noconfirm

# ======= Autostart Hyprland bila login via startx =======
echo 'exec hyprland' > ~/.xinitrc
chmod +x ~/.xinitrc
