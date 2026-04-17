# Blue Book References

Goldberg, Adele and Robson, David. *Smalltalk-80: The Language and its
Implementation.* Addison-Wesley, 1983. ISBN 0-201-11371-6.

## VM chapters

    26  The Formal Specification of the Smalltalk-80 Virtual Machine
    27  Specification of the Object Memory
    28  Formal Specification of the Interpreter
    29  Formal Specification of the Primitive Methods
    30  The Initial Image

## Citation style

When implementing or porting behavior from the Blue Book, cite it in
code as a short comment:

    // BB §28.4 — sendMessagesSelector:argumentCount:
    // BB Fig. 28.3 — message lookup

Do not reproduce Blue Book text. The page/section number is enough to
find the source.

## Online resources

- [dbanay/Smalltalk](https://github.com/dbanay/Smalltalk) —
  MIT-licensed C++ implementation we port from.
- [Wolczko's Smalltalk-80 image](http://www.wolczko.com/st80/image.tar.gz) —
  the canonical v2 virtual image all three reference implementations
  target.
- [Xerox Smalltalk-80 Porting Notes](http://www.wolczko.com/st80/notes.html) —
  documents the byte-swap and floating-point quirks we must handle in
  `ImageLoader`.
