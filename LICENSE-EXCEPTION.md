# Ravel: permission to link with the FM1 binary SDK

Ravel is free software under the GNU General Public License, version 3 only (`LICENSE`).
The files marked `GPL-3.0-only WITH GPL-3.0-linking-exception` also carry this additional
permission under section 7 of the GPL:

> If you modify this Program, or any covered work, by linking or combining it with M-VAVE's
> FM1 binary SDK (`sdk/lib/libfm1_engine.a` and the accompanying binary SDK components listed
> in the SDK's `sdk/manifest.json`, from https://github.com/m-vave/fm1_open), or a modified
> version of it, containing parts covered by the terms of that SDK's licence, the licensors of
> this Program grant you additional permission to convey the resulting work.

This is the same permission M-VAVE gives for its own FM1 app sources. It is what lets a Ravel
firmware package for the FM-1, which contains the SDK's binary library, be shared. Ravel and
its modifications remain under the GPL; the SDK keeps its own terms. A package must come with
`firmware/board/FM1-APP-LICENSE.md`, `firmware/board/FM1-THIRD_PARTY.md` and Ravel's source.
