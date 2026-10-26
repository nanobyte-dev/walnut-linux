# system/: the installed system as a whole: image/ (the root filesystem's package lists and per-image settings, `make rootfs`) and installer/ (walnut-install, which puts a release on the PS3's disk).
What each package installs lives in packages/; this is what ties them into one image.
Touch it to change what an image contains or how it is installed (docs/PACKAGING.md "Deploying and the image").
