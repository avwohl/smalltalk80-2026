# Windows resources

Binary art + manifests used by the Windows frontend and the
installer/Store packages live here (the `.rc` and `AppxManifest.xml`
references are relative to this directory).

Expected contents:

    st80.ico                 256x256 multi-res .ico used by:
                             - app/windows/st80-win.rc (ICON 1)
                             - NSIS installer header
                             - WiX MSI ARP entry

Drop the real icon in place before publishing. For development
builds the `.rc` guards the reference with `EXISTS`, so the app
compiles without it (the exe just gets a generic icon).

Generating an ICO from a 1024x1024 master PNG (ImageMagick):

    magick master.png -define icon:auto-resize=256,128,64,48,32,16 st80.ico
