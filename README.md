InstallIPA
==========
Works with .ipa and .deb files.

This sideloads apps if you have an Apple Developer account. I use it because its quicker than having to do all the work myself. This uses a few posix_spawn() calls, but shouldn't be an issue of security since this program never needs to interact with root.

I will probably update this in the future to be less messy and use less posix_spawn()'s. For now it "just works" (most of the time).