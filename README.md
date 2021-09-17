# UFO R

UFO R API.

## Building

Before building, retrievew the code of a submodule:

```bash
git submodule update --init --recursive
```

To update the submodule, pull it.

```bash
cd src/ufo-c
git pull
cd ../..
```

Install the package with R. This compiles and properly install the package.

```bash
R CMD INSTALL --preclean .
```

You can also build the project with debug symbols by setting (exporting) the `UFO_DEBUG` environmental variable to `1`.

```
UFO_DEBUG=1 R CMD INSTALL --preclean .
```
