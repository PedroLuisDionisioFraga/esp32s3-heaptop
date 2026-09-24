# Heaptop

[![Component Registry](https://components.espressif.com/components/pedroluisdionisiofraga/heaptop/badge.svg)](https://components.espressif.com/components/pedroluisdionisiofraga/heaptop)

Heaptop is an htop-like heap and task monitor for ESP-IDF over the serial console: live fragmentation, leak capture grouped by call stack, per-task heap, stack high-water mark and CPU usage, allocation rate and failure tracking, threshold alerts and a JSON Lines stream.

## Features

- TODO: list the main features of the component.

## Installation

Add the component to your project from the [ESP Component Registry](https://components.espressif.com/components/pedroluisdionisiofraga/heaptop):

```bash
idf.py add-dependency "pedroluisdionisiofraga/heaptop^0.1.0"
```

Or add it manually to your `main/idf_component.yml`:

```yaml
dependencies:
  pedroluisdionisiofraga/heaptop: "^0.1.0"
```

## Usage

```c
#include "heaptop.h"

void app_main(void)
{
  // TODO: minimal usage snippet.
}
```

## Examples

| Example | Description |
|---------|-------------|
| [basic](examples/basic) | Minimal usage of the component. |

Create a project from an example:

```bash
idf.py create-project-from-example "pedroluisdionisiofraga/heaptop:basic"
```

## API Reference

See [include/heaptop.h](include/heaptop.h) for the full public API.

## License

[MIT](LICENSE)
