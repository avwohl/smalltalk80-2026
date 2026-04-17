// st80-2026 — Oops.hpp
//
// Ported from dbanay/Smalltalk @ ab6ab55:src/oops.h
//   Copyright (c) 2020 Dan Banay. MIT License.
//   See THIRD_PARTY_LICENSES for full text.
// Modifications for st80-2026 Copyright (c) 2026 Aaron Wohl. MIT License.
//
// Well-known OOPs (Object Pointers) for the Xerox Smalltalk-80 v2
// image. These indices into the Object Table identify the VM's roots:
// nil / false / true, the special classes (SmallInteger, String, etc.),
// the special selectors (doesNotUnderstand:, mustBeBoolean), and the
// pointers the interpreter must be able to find without doing a
// dictionary lookup.
//
// Reference: Goldberg & Robson, "Smalltalk-80: The Language and its
// Implementation", p. 576 (initializeGuaranteedPointers) and the
// SystemTracer output reproduced in the trailing comment below.

#pragma once

namespace st80 {

// UndefinedObject and Booleans
inline constexpr int NilPointer = 2;
inline constexpr int FalsePointer = 4;
inline constexpr int TruePointer = 6;

// Root
inline constexpr int SchedulerAssociationPointer = 8;
inline constexpr int SmalltalkPointer = 25286;  // SystemDictionary

// Classes
inline constexpr int ClassSmallInteger = 12;
inline constexpr int ClassStringPointer = 14;
inline constexpr int ClassArrayPointer = 16;
inline constexpr int ClassFloatPointer = 20;
inline constexpr int ClassMethodContextPointer = 22;
inline constexpr int ClassBlockContextPointer = 24;
inline constexpr int ClassPointPointer = 26;
inline constexpr int ClassLargePositiveIntegerPointer = 28;
inline constexpr int ClassDisplayBitmapPointer = 30;
inline constexpr int ClassMessagePointer = 32;
inline constexpr int ClassCompiledMethod = 34;
inline constexpr int ClassSemaphorePointer = 38;
inline constexpr int ClassCharacterPointer = 40;
inline constexpr int ClassSymbolPointer = 56;
inline constexpr int ClassDisplayScreenPointer = 834;
inline constexpr int ClassUndefinedObject = 25728;

// Selectors
inline constexpr int DoesNotUnderstandSelector = 42;
inline constexpr int CannotReturnSelector = 44;
inline constexpr int MustBeBooleanSelector = 52;

// Tables
inline constexpr int SpecialSelectorsPointer = 48;
inline constexpr int CharacterTablePointer = 50;

// From dbanay: the first oops 2..52 are the specialObjects the
// SystemTracer emits when writing a Smalltalk-80 image:
//
//   specialObjects _
//        "1:" (Array with: nil with: false with: true
//                  with: (Smalltalk associationAt: #Processor))
//      , "5:" (Array with: Symbol table with: SmallInteger
//                  with: String with: Array)
//      , "9:" (Array with: (Smalltalk associationAt: #Smalltalk)
//                  with: Float with: MethodContext with: BlockContext)
//      , "13:" (Array with: Point with: LargePositiveInteger
//                  with: DisplayBitmap with: Message)
//      , "17:" (Array with: CompiledMethod with: #unusedOop18
//                  with: Semaphore with: Character)
//      , "21:" (Array with: #doesNotUnderstand: with: #cannotReturn:
//                  with: #monitor: with: Smalltalk specialSelectors)
//      , "25:" (Array with: Character characterTable
//                  with: #mustBeBoolean).
//   specialObjects size = 26 ifFalse: [self error: 'try again!!'].
//
// If a future GC is added beyond simple mark/sweep, these must be
// treated as roots.

}  // namespace st80
