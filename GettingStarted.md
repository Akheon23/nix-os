Nix has two components: a 64-bit AMD64 kernel, and a root file tree. You can use the file tree with a regular Plan 9 kernel or with 9vx; from there, you can build a 64-bit kernel.

# Downloading the source #

This part is simple! Just do

```
hg clone https://code.google.com/p/nix-os/ 
```

If you have a Google account, you can also check out like this:

```
hg clone https://your_username%40gmail.com@code.google.com/p/nix-os/ 
```

This should leave you with a `nix-os` directory under your current directory.

# Using 9vx #

You'll need to have [9vx](http://swtch.com/9vx/) installed. The best option is to download [Ron Minnich's repository](https://bitbucket.org/rminnich/vx32), build it, and put the `9vx` executable somewhere in your path (~/bin/ works well).

Once you have 9vx, you can simply run the `start9vx` script provided in the root of the Nix repository. This should launch a 9vx window running from the Nix root file tree.

At this point, you are running a full Plan 9 environment with our changes.

# Building libraries and kernels #

To keep the repository small, Nix does not include prebuilt binary libraries. You'll need to build them yourself before you can compile programs or kernels:

```
% cd /sys/src
% objtype=386
% mk libs
% cd ape/lib
% mk install
```

This should give you all you need to compile 386 programs and kernels.

If you want to build an AMD64 kernel, you'll also need to run

```
% cd /sys/src
% objtype=amd64
% mk install
```

to build all the AMD64 libs and programs. You can then do this to build a CPU kernel and the new PXE loader:

```
% cd /sys/src/nix/k10
% mk install # this should default to building 9k8cpu
% cd ../w/pxeload
% mk install
```

The resulting `ppxeload` bootloader and `9k8cpu` kernel can be used to netboot a 64-bit system.

# Staying up to date #

If you're not contributing code to Nix, you can simply do `hg pull -u` to update your repository to the latest version.