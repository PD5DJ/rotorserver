# N1MM Rotor Server — Installation Guide

Tested on **Raspberry Pi OS (Debian Trixie, 64-bit) with Desktop**.

---

## What does this software do?

The N1MM Rotor Server runs on a Raspberry Pi and controls your antenna rotors from N1MM Logger over the network. It consists of two parts:

- **n1mm_rotor_server** — the background service that talks to your rotor controllers via USB
- **n1mm_rotor_gui** — a graphical application that shows live rotor positions and lets you configure everything without using the command line

---

## Preparing the Raspberry Pi

This chapter covers everything you need to do before installing the N1MM Rotor Server: flashing the OS, assigning a fixed IP address, and setting up remote desktop access (VNC) — all without needing a monitor.

---

### Step 1 — Install Raspberry Pi OS (Trixie with Desktop)

**On your Windows or Mac PC:**

1. Download and install the **Raspberry Pi Imager** from [raspberrypi.com/software](https://www.raspberrypi.com/software/).

2. Insert your SD card (16 GB or larger) and open Raspberry Pi Imager.

3. Choose your hardware:
   - **Raspberry Pi Device** → select your model (e.g. Raspberry Pi 4 or 5)

4. Choose the operating system:
   - **Raspberry Pi OS (other)** → **Raspberry Pi OS (64-bit)**
   - Make sure this is the **Desktop** version (subtitle says "with desktop")
   - The current Trixie-based release is listed as Raspberry Pi OS (Debian Trixie, 64-bit)

5. Choose storage → select your SD card.

6. Click **Next**. When asked "Would you like to apply OS customisation settings?", click **Edit Settings**.

7. On the **General** tab, configure:

   | Field | Example |
   |---|---|
   | Hostname | `rotorpi` |
   | Username | `pi` (or your own name) |
   | Password | choose a strong password |
   | WiFi SSID / password | fill in if you want wireless during setup |
   | Locale / timezone | set to your region |

8. On the **Services** tab:
   - Enable **SSH** → select **Use password authentication**

9. Click **Save** → **Yes** → **Yes** to start flashing.

10. When done: eject the SD card, insert it in the Raspberry Pi, and power on.

> **No monitor needed.** SSH is active from first boot. Give the Pi 60–90 seconds to boot, then connect via SSH (see below).

---

### Step 2 — Connect via SSH

From your Windows PC, open **PowerShell** or **Command Prompt**:

```powershell
ssh pi@rotorpi.local
```

Replace `pi` with your username and `rotorpi` with the hostname you chose. If `.local` does not resolve, find the IP address from your router's DHCP client list, then use:

```powershell
ssh pi@192.168.1.xxx
```

Accept the host key fingerprint when prompted. You are now logged into the Pi remotely.

---

### Step 3 — Set a static IP address

A fixed IP address is essential: N1MM Logger needs to know where to send rotor commands, and that address must never change.

The Desktop version of Raspberry Pi OS uses **NetworkManager**. Use `nmcli` to configure a static address.

**Find the interface name and current connection:**

```bash
nmcli device status
```

Typical output:

```
DEVICE  TYPE      STATE      CONNECTION
eth0    ethernet  connected  Wired connection 1
wlan0   wifi      connected  MyWifi
lo      loopback  unmanaged  --
```

**Assign a static IP on the wired interface (`eth0`):**

```bash
# Show the exact connection name (copy it exactly, including spaces)
nmcli -g NAME connection show

# Apply static settings — adjust addresses to your network
sudo nmcli connection modify "Wired connection 1" \
  ipv4.method manual \
  ipv4.addresses 192.168.1.100/24 \
  ipv4.gateway 192.168.1.1 \
  ipv4.dns "8.8.8.8,8.8.4.4"

# Activate the new settings immediately
sudo nmcli connection up "Wired connection 1"
```

> Replace `192.168.1.100` with the address you want to assign. Make sure it is outside your router's DHCP range to avoid conflicts.

**Verify:**

```bash
ip a show eth0
```

The output should show your chosen address under `inet`.

> If you configured WiFi during imaging and want a static address on that interface too, repeat the steps above using the `wlan0` connection name.

**Reconnect your SSH session using the new address:**

```powershell
ssh pi@192.168.1.100
```

---

### Step 4 — Enable VNC (headless, no monitor required)

The N1MM Rotor Server comes with a graphical configuration tool (**n1mm_rotor_gui**) that makes adding and managing rotors much easier than working on the command line. To use it without a physical monitor connected to the Pi, you need VNC — a remote desktop protocol that lets you see and control the Pi's full graphical desktop from your Windows PC.

**Enable VNC via raspi-config:**

```bash
sudo raspi-config
```

Navigate to: **Interface Options** → **VNC** → **Yes** → **Finish**

**Configure a virtual display resolution (headless — no monitor connected):**

When no HDMI monitor is connected, the Pi has no display resolution by default, and VNC shows a tiny or blank screen. Fix this by setting a headless resolution:

```bash
sudo raspi-config
```

Navigate to: **Display Options** → **VNC Resolution** → choose a resolution (e.g. **1920×1080**) → **OK** → **Finish**

Reboot when prompted:

```bash
sudo reboot
```

Wait 30–60 seconds, then reconnect via SSH to confirm the Pi is back up.

**Alternatively — force HDMI resolution in `config.txt`:**

If `raspi-config` does not offer a headless resolution option, add these lines to `/boot/firmware/config.txt`:

```bash
sudo nano /boot/firmware/config.txt
```

Add at the bottom:

```ini
# Headless / no monitor: force HDMI output and resolution
hdmi_force_hotplug=1
hdmi_group=2
hdmi_mode=82
```

`hdmi_mode=82` corresponds to 1920×1080 60 Hz. Other common values: `35` = 1280×1024, `16` = 1024×768.

Save with **Ctrl+O**, exit with **Ctrl+X**, then reboot:

```bash
sudo reboot
```

**Connect from Windows using RealVNC Viewer:**

1. Download and install **RealVNC Viewer** from [realvnc.com/en/connect/download/viewer](https://www.realvnc.com/en/connect/download/viewer/).
2. Open RealVNC Viewer and enter the Pi's IP address: `192.168.1.100`
3. Log in with your Pi username and password.

You now see the full Raspberry Pi desktop on your Windows PC.

> **Tip:** You can also use **TigerVNC Viewer** or any other VNC client. The Pi's built-in VNC server (wayvnc or RealVNC, depending on Raspberry Pi OS version) accepts standard VNC connections.

---

## Installation

Open a terminal on the Raspberry Pi (or in your SSH/VNC session), go to the folder containing the source files, and run:

```bash
sudo bash install.sh
```

That's it. The installer handles everything automatically:

- Installs build tools and GTK libraries
- Builds the server and GUI
- Installs both programs system-wide
- Creates a default configuration file
- Grants your account access to USB serial ports
- Sets up the service so it starts automatically on every boot
- Configures the display environment so the GUI works correctly on Raspberry Pi

**When the installer finishes, log out and back in.** This activates serial port access.

---

## Adding your rotors

Start the graphical interface:

```bash
n1mm_rotor_gui
```

Click **+** in the title bar. A dialog appears where you fill in:

| Field | What to enter |
|---|---|
| Name | Any name you like, e.g. `2m yagi` |
| USB port | Select your rotor controller from the list |
| Protocol | `YAESU` (GS-232A/B) or `PROSISTEL` |
| Baud rate | Typically `9600` |
| Offset | Leave at `0` unless your rotor has a mechanical alignment offset |

Click **Save**. Repeat for each rotor. Then click **▶ Start** to start the server.

> **Important — always use the same USB port**
>
> The server identifies each rotor controller by the **physical USB port** it is plugged into, not by the device name or cable. When you add a rotor, the server records the exact physical location in the USB chain — which port on the Pi, and if a USB hub is used, also which port on that hub.
>
> This means:
> - Always plug each rotor controller back into **exactly the same port** it was in when you added it.
> - If you use a **USB hub**: the controller must always go back into the same hub port, **and** that hub must always stay connected to the same port on the Pi. Moving the hub to a different Pi port breaks the registration just as much as moving the controller itself.
> - If you need to move a controller or hub to a different port, use **Edit** (gear icon ⚙) on the rotor card to update the port assignment, or delete the rotor and add it again with the new port.

---

## Configuring N1MM Logger

N1MM Logger configuration consists of two parts: telling N1MM where the server is, and linking each antenna to the correct rotor by name.

### Part 1 — Connect to the rotor server

In N1MM Logger, go to:  
**Config → Configure Ports, Mode Control, Winkey, etc. → Broadcast Data**

Add an entry for the rotor server and fill in the server address including port number:

| Setting | Value |
|---|---|
| IP address + port | `192.168.1.100:12040` |

Use the static IP address you configured in Step 3. If you want to reach the server from any PC on the subnet without knowing the exact IP, you can use the subnet broadcast address instead:

```
192.168.1.255:12040
```

> Replace `192.168.1` with your own network segment.

### Part 2 — Link antennas to rotors by name

The rotor **Name** you entered in the GUI is the unique identifier that connects N1MM to the correct rotor. N1MM matches on this name exactly.

**Naming rules — read carefully:**
- The name is **case-sensitive**: `2mYagi` and `2myagi` are treated as two different rotors.
- The name must contain **no spaces**. Use letters, digits, or hyphens (e.g. `2m-Yagi`, `HF-Beam`).

In N1MM Logger, go to:  
**Config → Configure Ports, Mode Control, Winkey, etc. → Antennas**

For each antenna that uses a rotor, fill in the **Rotor Description** field with the rotor name **exactly as you entered it in the GUI** — same capitalisation, no spaces.

| GUI rotor name | Rotor Description in N1MM |
|---|---|
| `2m-Yagi` | `2m-Yagi` |
| `HF-Beam` | `HF-Beam` |

If the names do not match exactly, N1MM will not control the rotor.

---

## Using N1MM Logger and N1MM Rotor Client simultaneously

N1MM Logger and N1MM Rotor Client both need to receive rotor position broadcasts from the server. However, only one program at a time can open a UDP port — so both cannot listen on port 13010 at the same time.

The rotor server solves this with a **secondary broadcast port**: when configured, the server sends identical position broadcasts to both ports at the same time. You assign the primary port (13010) to N1MM Logger and the secondary port to N1MM Rotor Client, so they never interfere with each other.

**How to set it up:**

1. In the N1MM Rotor GUI, click the gear icon ⚙ in the title bar → **Server settings**.
2. Set the **Secondary broadcast port** to `13011` (or any other available port between 13011 and 13015).
3. Restart the server.

4. In **N1MM Logger**: leave the broadcast port at `13010` (default, no change needed).
5. In **N1MM Rotor Client**: set the broadcast port to `13011`.

The server now broadcasts every position update on both ports simultaneously. N1MM Logger and N1MM Rotor Client each receive their own copy without conflicting.

> The secondary broadcast port is disabled by default (set to `Off`). It only needs to be enabled when you run N1MM Rotor Client alongside N1MM Logger.

---

## Daily use

The GUI shows the live azimuth of every rotor. You can:

- **Start / stop** the server with the buttons in the title bar
- **Add a rotor** with the **+** button
- **Edit a rotor** by clicking the gear icon ⚙ on its card
- **Delete a rotor** by clicking the trash icon on its card

After changing the configuration, the GUI will ask you to restart the server for the changes to take effect.

---

## Server settings (optional)

The default settings work for most setups. To change ports or broadcast addresses, click the gear icon ⚙ in the title bar → **Server settings**.

| Setting | Default | Description |
|---|---|---|
| Command port | 12040 | Port on which N1MM sends commands (UDP) |
| Primary broadcast port | 13010 | Port on which positions are sent to N1MM (fixed) |
| Secondary broadcast port | Off | Optional extra broadcast port (13011–13015) for a second client |

---

## Useful commands

```bash
# Check whether the server is running
systemctl status n1mm_rotor_server

# Watch the live log
journalctl -u n1mm_rotor_server -f

# Uninstall
sudo make uninstall
```

---

## Troubleshooting

**GUI does not start / blank screen**  
Make sure you logged out and back in after installation. The display environment is set during login.

**Rotor controller not found in the USB list**  
Check that the USB cable is plugged in. If the list is empty, serial port access may not have taken effect yet — log out and back in.

**N1MM does not receive position updates**  
Check that the IP address and port numbers in N1MM match the server settings. Make sure no firewall is blocking UDP port 13010.

**Rotor moves to wrong direction**  
Use the **Offset** setting on the rotor card (gear icon ⚙) to correct mechanical misalignment. Positive offset rotates the reported heading clockwise.
