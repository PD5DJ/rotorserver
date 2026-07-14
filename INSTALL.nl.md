# N1MM Rotor Server — Installatiehandleiding

Getest op **Raspberry Pi OS (Debian Trixie, 64-bit) met Desktop**.

---

## Wat doet deze software?

De N1MM Rotor Server draait op een Raspberry Pi en bestuurt je antenne-rotoren vanuit N1MM Logger over het netwerk. Het bestaat uit twee onderdelen:

- **n1mm_rotor_server** — de achtergrondservice die via USB met je rotorcontrollers praat
- **n1mm_rotor_gui** — een grafische applicatie die live rotorposities toont en waarmee je alles kunt configureren zonder de commandoregel te gebruiken

---

## Raspberry Pi voorbereiden

Dit hoofdstuk behandelt alles wat je moet doen vóór de installatie van de N1MM Rotor Server: het OS flashen, een vast IP-adres instellen en toegang via remote desktop (VNC) activeren — allemaal zonder monitor nodig te hebben.

---

### Stap 1 — Raspberry Pi OS installeren (Trixie met Desktop)

**Op je Windows- of Mac-pc:**

1. Download en installeer de **Raspberry Pi Imager** van [raspberrypi.com/software](https://www.raspberrypi.com/software/).

2. Steek je SD-kaart (16 GB of groter) in en open de Raspberry Pi Imager.

3. Kies je hardware:
   - **Raspberry Pi Device** → kies je model (bijv. Raspberry Pi 4 of 5)

4. Kies het besturingssysteem:
   - **Raspberry Pi OS (other)** → **Raspberry Pi OS (64-bit)**
   - Zorg dat dit de **Desktop**-versie is (ondertitel vermeldt "with desktop")
   - De huidige Trixie-release staat vermeld als Raspberry Pi OS (Debian Trixie, 64-bit)

5. Kies opslag → selecteer je SD-kaart.

6. Klik op **Next**. Als gevraagd wordt "Would you like to apply OS customisation settings?", klik je op **Edit Settings**.

7. Vul op het tabblad **General** in:

   | Veld | Voorbeeld |
   |---|---|
   | Hostname | `rotorpi` |
   | Gebruikersnaam | `pi` (of je eigen naam) |
   | Wachtwoord | kies een sterk wachtwoord |
   | WiFi SSID / wachtwoord | invullen als je tijdens de installatie draadloos wilt verbinden |
   | Locale / tijdzone | stel in op je regio |

8. Op het tabblad **Services**:
   - Zet **SSH** aan → kies **Use password authentication**

9. Klik op **Save** → **Yes** → **Yes** om te beginnen met flashen.

10. Als het klaar is: verwijder de SD-kaart, steek hem in de Raspberry Pi en zet hem aan.

> **Geen monitor nodig.** SSH is actief vanaf de eerste keer opstarten. Geef de Pi 60–90 seconden om op te starten en maak daarna verbinding via SSH (zie hieronder).

---

### Stap 2 — Verbinding maken via SSH

Open op je Windows-pc **PowerShell** of **Opdrachtprompt**:

```powershell
ssh pi@rotorpi.local
```

Vervang `pi` door je gebruikersnaam en `rotorpi` door de hostname die je hebt gekozen. Als `.local` niet werkt, zoek het IP-adres op in de DHCP-clientlijst van je router en gebruik dan:

```powershell
ssh pi@192.168.1.xxx
```

Bevestig de hostsleutelfingerafdruk als daarom gevraagd wordt. Je bent nu op afstand ingelogd op de Pi.

---

### Stap 3 — Statisch IP-adres instellen

Een vast IP-adres is onmisbaar: N1MM Logger moet weten waar de rotoropdrachten naartoe gestuurd worden, en dat adres mag nooit veranderen.

De Desktop-versie van Raspberry Pi OS gebruikt **NetworkManager**. Gebruik `nmcli` om een statisch adres in te stellen.

**Zoek de interfacenaam en huidige verbinding:**

```bash
nmcli device status
```

Typische uitvoer:

```
DEVICE  TYPE      STATE      CONNECTION
eth0    ethernet  connected  Wired connection 1
wlan0   wifi      connected  MijnWifi
lo      loopback  unmanaged  --
```

**Stel een statisch IP in op de bedrade interface (`eth0`):**

```bash
# Toon de exacte verbindingsnaam (kopieer precies, inclusief spaties)
nmcli -g NAME connection show

# Statische instellingen toepassen — pas adressen aan je netwerk aan
sudo nmcli connection modify "Wired connection 1" \
  ipv4.method manual \
  ipv4.addresses 192.168.1.100/24 \
  ipv4.gateway 192.168.1.1 \
  ipv4.dns "8.8.8.8,8.8.4.4"

# Nieuwe instellingen direct activeren
sudo nmcli connection up "Wired connection 1"
```

> Vervang `192.168.1.100` door het adres dat je wilt toewijzen. Zorg dat het buiten het DHCP-bereik van je router valt om conflicten te voorkomen.

**Controleer:**

```bash
ip a show eth0
```

De uitvoer moet je gekozen adres tonen onder `inet`.

> Als je tijdens het flashen ook WiFi hebt geconfigureerd en daar ook een statisch adres op wilt, herhaal dan de stappen hierboven met de verbindingsnaam van `wlan0`.

**Maak opnieuw verbinding via SSH met het nieuwe adres:**

```powershell
ssh pi@192.168.1.100
```

---

### Stap 4 — VNC inschakelen (headless, zonder monitor)

De N1MM Rotor Server wordt geleverd met een grafisch configuratieprogramma (**n1mm_rotor_gui**) waarmee je rotoren veel eenvoudiger kunt toevoegen en beheren dan via de commandoregel. Om dit programma te kunnen gebruiken zonder dat er een fysieke monitor op de Pi is aangesloten, heb je VNC nodig — een remote desktop-protocol waarmee je het volledige grafische bureaublad van de Pi kunt zien en bedienen vanaf je Windows-pc.

**VNC inschakelen via raspi-config:**

```bash
sudo raspi-config
```

Navigeer naar: **Interface Options** → **VNC** → **Yes** → **Finish**

**Virtuele schermresolutie instellen (headless — geen monitor aangesloten):**

Als er geen HDMI-monitor is aangesloten, heeft de Pi standaard geen schermresolutie en toont VNC een klein of leeg scherm. Los dit op door een headless resolutie in te stellen:

```bash
sudo raspi-config
```

Navigeer naar: **Display Options** → **VNC Resolution** → kies een resolutie (bijv. **1920×1080**) → **OK** → **Finish**

Herstart wanneer daarom gevraagd wordt:

```bash
sudo reboot
```

Wacht 30–60 seconden en maak daarna opnieuw verbinding via SSH om te bevestigen dat de Pi weer actief is.

**Alternatief — HDMI-resolutie forceren via `config.txt`:**

Als `raspi-config` geen headless resolutie-optie biedt, voeg dan deze regels toe aan `/boot/firmware/config.txt`:

```bash
sudo nano /boot/firmware/config.txt
```

Voeg onderaan toe:

```ini
# Headless / geen monitor: forceer HDMI-uitvoer en resolutie
hdmi_force_hotplug=1
hdmi_group=2
hdmi_mode=82
```

`hdmi_mode=82` staat voor 1920×1080 60 Hz. Andere veelgebruikte waarden: `35` = 1280×1024, `16` = 1024×768.

Opslaan met **Ctrl+O**, afsluiten met **Ctrl+X**, daarna herstarten:

```bash
sudo reboot
```

**Verbinding maken vanuit Windows met RealVNC Viewer:**

1. Download en installeer **RealVNC Viewer** van [realvnc.com/en/connect/download/viewer](https://www.realvnc.com/en/connect/download/viewer/).
2. Open RealVNC Viewer en voer het IP-adres van de Pi in: `192.168.1.100`
3. Log in met je Pi-gebruikersnaam en -wachtwoord.

Je ziet nu het volledige Raspberry Pi-bureaublad op je Windows-pc.

> **Tip:** Je kunt ook **TigerVNC Viewer** of een andere VNC-client gebruiken. De ingebouwde VNC-server van de Pi (wayvnc of RealVNC, afhankelijk van de Raspberry Pi OS-versie) accepteert standaard VNC-verbindingen.

---

## Installatie

Open een terminal op de Raspberry Pi (of in je SSH/VNC-sessie), ga naar de map met de bronbestanden en voer uit:

```bash
sudo bash install.sh
```

Dat is alles. Het installatiescript regelt alles automatisch:

- Installeert buildtools en GTK-bibliotheken
- Bouwt de server en de GUI
- Installeert beide programma's systeembreed
- Maakt een standaard configuratiebestand aan
- Geeft je account toegang tot USB-seriële poorten
- Stelt de service in zodat hij automatisch start bij elke herstart
- Configureert de weergaveomgeving zodat de GUI correct werkt op Raspberry Pi

**Als het installatiescript klaar is, log je uit en opnieuw in.** Dit activeert de toegang tot seriële poorten.

---

## Rotoren toevoegen

Start de grafische interface:

```bash
n1mm_rotor_gui
```

Klik op **+** in de titelbalk. Er verschijnt een dialoogvenster waar je het volgende invult:

| Veld | Wat je invult |
|---|---|
| Naam | Een naam naar keuze, bijv. `2m yagi` |
| USB-poort | Kies je rotorcontroller uit de lijst |
| Protocol | `YAESU` (GS-232A/B) of `PROSISTEL` |
| Baudrate | Doorgaans `9600` |
| Offset | Laat op `0` staan, tenzij je rotor een mechanische uitlijningsafwijking heeft |

Klik op **Save**. Herhaal dit voor elke rotor. Klik daarna op **▶ Start** om de server te starten.

> **Belangrijk — gebruik altijd dezelfde USB-poort**
>
> De server herkent elke rotorcontroller aan de hand van de **fysieke USB-poort** waarop deze is aangesloten, niet op basis van de apparaatnaam of kabel. Wanneer je een rotor toevoegt, legt de server de exacte fysieke locatie in de USB-keten vast — welk aansluitpunt op de Pi, en als er een USB-hub wordt gebruikt, ook welk poort op die hub.
>
> Dit betekent:
> - Sluit elke rotorcontroller altijd terug aan op **exact dezelfde poort** als waarop deze zat toen je de rotor hebt toegevoegd.
> - Gebruik je een **USB-hub**: de controller moet altijd terug in dezelfde hubpoort, **én** die hub moet altijd aangesloten blijven op dezelfde poort van de Pi. De hub naar een andere Pi-poort verplaatsen verbreekt de registratie net zo goed als de controller zelf verplaatsen.
> - Als je een controller of hub toch naar een andere poort moet verplaatsen, gebruik dan **Bewerken** (tandwielicoon ⚙) op de rotorkaart om de poortregistratie bij te werken, of verwijder de rotor en voeg hem opnieuw toe met de nieuwe poort.

---

## N1MM Logger configureren

De configuratie in N1MM Logger bestaat uit twee onderdelen: N1MM vertellen waar de server staat, en elke antenne aan de juiste rotor koppelen via de naam.

### Deel 1 — Verbinding met de rotorserver instellen

Open N1MM Logger en ga naar:  
**Config → Configure Ports, Mode Control, Winkey, etc. → Broadcast Data**

Voeg een regel toe voor de rotorserver en vul het serveradres in inclusief poortnummer:

| Instelling | Waarde |
|---|---|
| IP-adres + poort | `192.168.1.100:12040` |

Gebruik het statische IP-adres dat je in Stap 3 hebt ingesteld. Als je de server wilt bereiken vanaf elke pc op het subnet zonder het exacte IP te weten, kun je het subnet-broadcastadres gebruiken:

```
192.168.1.255:12040
```

> Vervang `192.168.1` door je eigen netwerksegment.

### Deel 2 — Antennes aan rotoren koppelen via naam

De rotor**naam** die je in de GUI hebt ingevoerd is het unieke ID waarmee N1MM de juiste rotor herkent. N1MM vergelijkt deze naam teken voor teken.

**Naamregels — let goed op:**
- De naam is **hoofdlettergevoelig**: `2mYagi` en `2myagi` worden als twee verschillende rotoren beschouwd.
- De naam mag **geen spaties** bevatten. Gebruik letters, cijfers of koppeltekens (bijv. `2m-Yagi`, `HF-Beam`).

Ga in N1MM Logger naar:  
**Config → Configure Ports, Mode Control, Winkey, etc. → Antennas**

Vul voor elke antenne die een rotor gebruikt het veld **Rotor Description** in met de rotornaam **exact zoals je die in de GUI hebt ingevoerd** — zelfde hoofdletters, geen spaties.

| Rotornaam in de GUI | Rotor Description in N1MM |
|---|---|
| `2m-Yagi` | `2m-Yagi` |
| `HF-Beam` | `HF-Beam` |

Als de namen niet exact overeenkomen, zal N1MM de rotor niet aansturen.

---

## N1MM Logger en N1MM Rotor Client tegelijk gebruiken

Zowel N1MM Logger als N1MM Rotor Client willen positie-broadcasts van de server ontvangen. Maar slechts één programma tegelijk kan een UDP-poort openen — ze kunnen dus niet allebei op poort 13010 luisteren.

De rotorserver lost dit op met een **secundaire broadcast-poort**: wanneer ingesteld, stuurt de server identieke positie-broadcasts gelijktijdig naar beide poorten. Je wijst de primaire poort (13010) toe aan N1MM Logger en de secundaire poort aan N1MM Rotor Client, zodat ze elkaar nooit in de weg zitten.

**Hoe stel je dit in:**

1. Klik in de N1MM Rotor GUI op het tandwielicoon ⚙ in de titelbalk → **Server settings**.
2. Stel de **Secondary broadcast port** in op `13011` (of een andere beschikbare poort tussen 13011 en 13015).
3. Herstart de server.

4. In **N1MM Logger**: laat de broadcast-poort op `13010` staan (standaard, niets wijzigen).
5. In **N1MM Rotor Client**: stel de broadcast-poort in op `13011`.

De server stuurt nu elke positie-update gelijktijdig uit op beide poorten. N1MM Logger en N1MM Rotor Client ontvangen elk hun eigen kopie zonder conflicten.

> De secundaire broadcast-poort staat standaard uitgeschakeld (ingesteld op `Uit`). Deze hoef je alleen in te schakelen als je N1MM Rotor Client tegelijk met N1MM Logger gebruikt.

---

## Dagelijks gebruik

De GUI toont de live azimut van elke rotor. Je kunt:

- **De server starten / stoppen** met de knoppen in de titelbalk
- **Een rotor toevoegen** met de **+**-knop
- **Een rotor bewerken** door op het tandwielicoon ⚙ op de kaart te klikken
- **Een rotor verwijderen** door op het prullenbakicoon op de kaart te klikken

Na het wijzigen van de configuratie zal de GUI vragen om de server opnieuw te starten voordat de wijzigingen actief worden.

---

## Serverinstellingen (optioneel)

De standaardinstellingen werken voor de meeste situaties. Om poorten of broadcast-adressen te wijzigen, klik je op het tandwielicoon ⚙ in de titelbalk → **Server settings**.

| Instelling | Standaard | Beschrijving |
|---|---|---|
| Command port | 12040 | Poort waarop N1MM opdrachten stuurt (UDP) |
| Primary broadcast port | 13010 | Poort waarop posities naar N1MM worden gestuurd (vast) |
| Secondary broadcast port | Uit | Optionele extra broadcast-poort (13011–13015) voor een tweede client |

---

## Handige commando's

```bash
# Controleer of de server actief is
systemctl status n1mm_rotor_server

# Bekijk het live logboek
journalctl -u n1mm_rotor_server -f

# Deïnstalleren
sudo make uninstall
```

---

## Problemen oplossen

**GUI start niet / leeg scherm**  
Zorg dat je na de installatie bent uitgelogd en opnieuw ingelogd. De weergaveomgeving wordt ingesteld tijdens het inloggen.

**Rotorcontroller niet gevonden in de USB-lijst**  
Controleer of de USB-kabel is aangesloten. Als de lijst leeg is, is de toegang tot seriële poorten mogelijk nog niet actief — log uit en opnieuw in.

**N1MM ontvangt geen positie-updates**  
Controleer of het IP-adres en de poortnummers in N1MM overeenkomen met de serverinstellingen. Zorg dat geen firewall UDP-poort 13010 blokkeert.

**Rotor draait de verkeerde kant op**  
Gebruik de **Offset**-instelling op de rotorkaart (tandwielicoon ⚙) om mechanische uitlijningsafwijking te corrigeren. Een positieve offset draait de gerapporteerde richting met de klok mee.
