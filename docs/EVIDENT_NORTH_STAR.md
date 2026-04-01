# Evident North Star

Status: Aspirational design note

Last updated: 2026-03-29

## Purpose

This document captures what Evident should feel like at its best.

It is not the current grammar. It is not a compatibility promise. It is not a checklist for the next patch series. It is the thing to preserve if the language is rewritten.

Evident should feel like it was designed by someone who cares about typography, reads like prose, has the rigor of a proof assistant, and makes a well-written file rewarding to look at.

Correctness is not enough. A good Evident file should make the reader want to keep reading.

## What Must Survive A Rewrite

- A name should feel like a claim, not a label.
- Proof, authority, failure, and state should remain explicit in both syntax and semantics.
- The file should read like an argument or a small book, not a bag of tokens.
- Invariants should have names worth rereading.
- Layout should breathe. Whitespace, alignment, and grouping should help the eye understand the proof shape.
- The language should reward restraint. A clear file should feel settled, not clever.
- The most important parts of a file should be its obligations, laws, witnesses, and conclusions.

## Design Direction

The exact surface syntax below is intentionally ahead of the current compiler. It is a specimen, not a promise. The point is the posture:

- file-level preambles that state intent before mechanism
- declaration forms that can carry local laws and named theorems
- proof steps that read like reasoning, not just construction
- final conclusions that feel earned
- typography that makes a source file resemble a page of doctrine

If a future redesign uses different keywords, punctuation, or block forms but preserves those qualities, it is still following this north star.

## Specimen

```evd
file   "first_light.evd"
title  "First Light"
motto  "A name is a claim; keep it honest."

about
    This file does not begin with machinery.

    It begins with an obligation:
    if we say it is morning,
    we must be able to show why.
end


------------------------------------------------------------
book i. what may fail
------------------------------------------------------------

public reason DawnDenied
    | no_signal
        station : Station

    | unsettled_light
        station : Station
        pulse   : Pulse

    | contradiction
        station : Station
        earlier : Lux
        later   : Lux
end


------------------------------------------------------------
book ii. what may be held
------------------------------------------------------------

public permit JournalOpen


public proof Dawn
    station : Station
    hour    : Hour
    light   : Lux

    law enough_light :
        light >= 12

    law proper_hour :
        4 <= hour and hour <= 9
end


public record Line
    text   : Text
    source : Station

    law text_is_worth_keeping :
        text != ""
end


public record Morning
    line    : Line
    witness : Dawn

    theorem honest_naming :
        every Morning carries the proof that licensed its name
end


------------------------------------------------------------
book iii. observation
------------------------------------------------------------

fn observe (station : Station) -> SkySample
    yields DawnDenied
=
    inspect Sky.read station
    | dark =>
        fail no_signal
            station = station

    | trembling pulse =>
        fail unsettled_light
            station = station
            pulse   = pulse

    | incoherent earlier later =>
        fail contradiction
            station = station
            earlier = earlier
            later   = later

    | rising sample =>
        sample
end


------------------------------------------------------------
book iv. certification
------------------------------------------------------------

fn attest (sample : SkySample) -> Dawn
    proves Dawn
=
    admit Dawn
        station = sample.station
        hour    = sample.hour
        light   = sample.light

    because
        sample.light >= 12

    and
        4 <= sample.hour and sample.hour <= 9
end


fn compose (sample : SkySample) -> Line =
    Line
        text   = """
                 The day did not begin because we wanted it to.
                 It began because light, at last, became sufficient.
                 """
        source = sample.station
end


------------------------------------------------------------
book v. saying so
------------------------------------------------------------

public fn begin (station : Station) -> Morning
    yields DawnDenied
=
    let sample = try observe station
    let dawn   = attest sample
    let line   = compose sample

    with Journal.open() as journal
        Journal.append journal, line.text

    therefore
        Morning
            line    = line
            witness = dawn
end
```

## Reading Test

When evaluating a future syntax or file format, ask:

- Does this read like prose with obligations?
- Can a reader see what is being claimed, permitted, proven, and concluded?
- Do the important invariants feel named and local?
- Does the page look calm?
- Would someone be happy to read a file like this every morning?

If the answer is no, the design may still be correct, but it is not yet Evident's north star.
