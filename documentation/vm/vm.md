---
title: VM
layout: default
documentation: true
categories: [VM]
---

If you just want to try Storm without installing it or its dependencies, the virtual machine image we provide might be the right fit for you. We pre-installed Storm, its dependencies and other useful reference tools (like [PRISM](http://www.prismmodelchecker.org/) and [IMCA](https://github.com/buschko/imca) and the PRISM benchmark suite) on a Linux host system. You can download the latest version of the virtual machine [here](https://rwth-aachen.sciebo.de/index.php/s/nthEAQL4o49zkYp).

{:.alert .alert-info}
The virtual machine is hosted at [sciebo](https://www.sciebo.de/en/), an academic cloud hoster. We are not able to trace the identity of downloaders, so reviewers can use this link without revealing their identity.

## Importing

When you have downloaded the OVA image, you can import it into, for example, [VirtualBox](https://www.virtualbox.org) and run it. The username and password are both *storm* and a `README` file is provided in the home folder of the user *storm*. In the virtual machine, Storm is located at `/home/storm/storm` and the binaries can be found in `/home/storm/storm/build/bin`. For your convenience, an environment variable with the name `STORM_DIR` is set to the path containing the binaries and this directory is added to the `PATH`, meaning that you can run the Storm binaries from any location in the terminal and that `cd $STORM_DIR` will take you to the folders containing Storm's binaries. For more information on how to run Storm, please read our [guide]({{ site.baseurl }}/documentation/usage/running-storm.html).

## Changelog

The VM is periodically updated to include bug fixes, new versions, and so on. When the image was most recently updated and what changes were made to the VM can be taken from the following changelog.

#### Update on Feb 1, 2017

- updated to newest Storm version
- added files containing all tool invocations used in [benchmarks]({{ site.baseurl }}/benchmarks.html)
- installed latest version of [IMCA](https://github.com/buschko/imca) and added its benchmark files

#### Update on Jan 22, 2017

- installed Storm
- installed [PRISM v4.3.1](http://www.prismmodelchecker.org/download.php)
- added [PRISM benchmark suite](https://github.com/prismmodelchecker/prism-benchmarks/)