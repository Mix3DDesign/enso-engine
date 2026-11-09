# Ensō Engine

**Ensō Engine** is a free anime and NPR build of [Blender](https://www.blender.org), made by **Mix 3D Design**.

It is Blender 5.3 with anime rendering built in: cel shading tools, the Ensō Outline compositor node and modifier, an anime-first interface and theme, workspaces set up for stylized work, and support for Ensō DLC packs.

- Website: https://enso.mix3ddesign.com
- Download (Windows installer and portable): https://mix3ddesign.com/posts/enso-engine-1-0-174746439
- YouTube: https://www.youtube.com/@Mix3Design
- Patreon: https://www.patreon.com/Mix3dDesign

## License

Ensō Engine is based on Blender and is released under the **GNU General Public License, version 3 or later**, the same license as Blender. Individual files may carry a different but compatible license. See [`COPYING`](COPYING) and [blender.org/about/license](https://www.blender.org/about/license/).

This repository is the complete source code for Ensō Engine releases. You are free to study, change and share it under the terms of the GPL.

Ensō Engine is an independent project by Mix 3D Design. It is not made or endorsed by the Blender Foundation. "Blender" is a trademark of the Blender Foundation.

## About the Ensō NPR Pack

The optional **Ensō NPR Pack** (cel shaders, Anime Sky Pro, rain, ocean, motion VFX and more) is a separate add-on pack sold by Mix 3D Design. It is not part of this repository. Ensō Engine works on its own without it.

## Building

Ensō builds exactly like Blender. Follow Blender's guide for your platform:
https://developer.blender.org/docs/handbook/building_blender/

On Windows, in short:

```
git clone https://github.com/Mix3DDesign/enso-engine.git
cd enso-engine
git checkout enso-engine
make update
make
```

This branch is based on Blender's `blender-v5.3-release` branch.

> Tip: if cloning from GitHub gives Git LFS errors, set `GIT_LFS_SKIP_SMUDGE=1` for the first clone, as Blender's own GitHub mirror recommends.

## Where the Ensō changes live

- `scripts/startup/bl_operators/enso_*.py`: Ensō DLC packs, theme and workspaces
- `scripts/modules/enso_*.py`: Ensō helpers, including pack licensing
- `source/blender/`: native Ensō features such as the Ensō Outline node, branding and UI changes
- `release/`: Ensō branding, splash and Windows packaging

## Credits

Built on the work of the Blender Foundation and the Blender community. Ensō Engine changes by Mix 3D Design.
