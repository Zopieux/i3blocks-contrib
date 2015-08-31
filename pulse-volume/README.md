# pulse-volume

Listen for changes of a pulseaudio sink (i.e. output device) volume.

This is a long running process that never exits, until you kill it. The volume
value is printed every time it changes. This is why you must use
`interval=persist`.

``` ini
[pulse-volume]
command=./pulse-volume
interval=persist
label=♪
```

## Building

Run `make` in the script directory to build the program binary. The only
dependency is `libpulse`.

## Options

Show usage by running:

```console
./pulse-volume -h
```

- By default sink 0 is used, which should be your primary audio output.
  You can change the sink index by adding `-s INDEX`. Run `pacmd list-sinks` to
  show the available sinks.
- By default the program displays the *average* of the various channel volumes,
  eg. left & right. You can use the minimum or maximum instead, by adding resp.
  `-m min` or `-m max`.
- By default, a `0` to `100` percentage is displayed. You can use a *decibel*
  scale instead (`-∞` to `0.00`) by adding `-d`.
