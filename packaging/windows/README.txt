Desk - Windows x64

Run Desk-Setup-x64.exe to install for the current user.
The application, settings, pairing identity and uninstall entry are separate from Moonlight.
Sunshine is built from the pinned source submodule and included under host/sunshine.
No Sunshine service, driver, startup task or firewall rule is installed automatically.

Local test:
1. Open "This computer" and leave "Local test only" enabled. Start hosting.
2. Open "Configuration & pairing" and create the Sunshine login.
3. Click "Add this computer". Pair in Devices, then enter the PIN in Sunshine's web UI.
4. Connect desktop. Streaming this display back to itself creates a mirror effect.

Desk host: 127.0.0.1:48989
Local web configuration: https://127.0.0.1:48990 (Sunshine uses a self-signed certificate)
To connect from another PC, stop hosting, switch off "Local test only", restart,
and add WINDOWS_LAN_IP:48989 in Moonlight on the other PC.
Allow the bundled Sunshine through Windows Firewall on private networks if prompted.

Desk only hosts while the app is running and you are signed in.
Quit the stream before stopping hosting. Screen lock, UAC/secure desktop,
controller drivers and real GPU streaming require testing on the target PC.
The GitHub build runner checks packaging and process management, not GPU streaming.
The installer is unsigned and may trigger Windows SmartScreen.

Source: https://github.com/coderleexl/moonlight-qt
Sunshine source: https://github.com/LizardByte/Sunshine
