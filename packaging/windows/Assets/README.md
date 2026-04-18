# Windows Store / MSIX assets

The `.msix` you upload to Partner Center expects a fixed set of
logo PNGs in the AppX package. Drop real artwork in here with
these exact filenames:

    StoreLogo.png           50 x 50       Store listing tile
    Square44x44Logo.png     44 x 44       taskbar + list entries
    Square71x71Logo.png     71 x 71       small tile
    Square150x150Logo.png  150 x 150      medium tile  (default)
    Square310x310Logo.png  310 x 310      large tile
    Wide310x150Logo.png    310 x 150      wide tile
    SplashScreen.png       620 x 300      boot splash

The CMake target `st80_appx_layout` copies this whole directory
into `build/st80-<ver>-appx/Assets/` next to `AppxManifest.xml`.
Placeholder / transparent PNGs are acceptable for side-loaded
builds and smoke tests — the Store itself will reject the
submission if any are missing or undersized, so the error is
caught at upload time rather than at pack time.

Generating from a single 1024x1024 master:

    # requires ImageMagick
    convert master.png -resize 50x50    StoreLogo.png
    convert master.png -resize 44x44    Square44x44Logo.png
    convert master.png -resize 71x71    Square71x71Logo.png
    convert master.png -resize 150x150  Square150x150Logo.png
    convert master.png -resize 310x310  Square310x310Logo.png
    convert master.png -resize 310x150  Wide310x150Logo.png
    convert master.png -resize 620x300  SplashScreen.png
