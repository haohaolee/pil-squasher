## PIL squasher

**pil-squasher** takes a split firmware image (mdt + bXX files) and squash them
into a single mbn firmware image, preserving signature et al.

## PIL splitter

**pil-splitter** takes a single mbn firmware image and split it into mdt + bXX
files, the reverse operation of pil-squasher.

## Usage

```bash
pil-squasher <mbn output> <mdt input>
pil-splitter <mbn input> <mdt output>
```

## Credits

port from https://github.com/linux-msm/pil-squasher
