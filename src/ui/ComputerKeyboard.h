// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#pragma once

#include <JuceHeader.h>

namespace nightshift
{

/** The PC keyboard as a music keyboard, its top two letter rows laid out like a piano's keys:
    A S D F G H J K L ; ' are the white keys from C, and above them W E, T Y U, O P and ] the
    black ones (Q, R, I and [ sit where a piano has no black key, and do nothing). Z and X move
    the octave, C and V the velocity. A held key holds its note, whatever the octave does
    meanwhile. Plain keys are music; with Shift, Ctrl or Alt held, keys are shortcuts.

    It reads the keys themselves (which are down), so it plays whichever of Nightshift's
    windows is in front, a plugin's included, as FL's typing keyboard does: the studio calls
    poll() as keys change and many times a second besides. */
class ComputerKeyboard
{
public:
    ComputerKeyboard() = default;

    std::function<void (int key, int velocity)> onNoteOn;
    std::function<void (int key)> onNoteOff;

    /** Switched on or off. */
    std::function<void()> onChanged;

    /** The octave or velocity moved. */
    std::function<void()> onMoved;

    void setEnabled (bool);
    bool isEnabled() const                      { return enabled; }

    /** The note A plays (60 is middle C, C5), and how hard every note plays. */
    int getLowestKey() const                    { return lowest; }
    int getVelocity() const                     { return velocity; }

    /** How many notes it's holding. */
    int getNotesHeld() const                    { return (int) sounding.size(); }

    /** "C5 to F6, velocity 100". */
    juce::String describe() const;

    /** A key press reaching the studio: true if it's one of the keyboard's, so it isn't taken
        as a shortcut too. */
    bool keyPressed (const juce::KeyPress&);

    /** Brings the notes in line with the keys held, and moves the octave or velocity for a
        key just pressed; while text is being typed into one of Nightshift's fields, or another
        app is in front, it stops every note instead. Returns true if any of its keys is
        involved. */
    bool poll (bool typingIntoText);

    /** Every note it's playing stops. */
    void releaseAll();

    /** Whether a key (its character) is down, and whether Nightshift is the app in front. The
        real answers unless a test stands in. */
    std::function<bool (int character)> isKeyDown = [] (int c) { return juce::KeyPress::isKeyCurrentlyDown (c); };
    std::function<bool()> isAppInFront = [] { return juce::Process::isForegroundProcess(); };

    /** The note keys, lowest first, a semitone apart; the keys in their rows that play nothing
        (where a piano has no black key); then octave down and up, velocity down and up. */
    static constexpr const char* noteKeys = "awsedftgyhujkolp;']";
    static constexpr const char* gapKeys = "qri[\\";
    static constexpr const char* controlKeys = "zxcv";

    /** The velocities C and V step through. */
    static constexpr int velocities[] = { 1, 20, 40, 60, 80, 100, 127 };

private:
    bool enabled = true;
    int lowest = 60, velocity = 100;
    std::map<char, int> sounding;               // key held -> the note it started
    std::set<char> controlsDown;                // octave and velocity keys held (they act once per press)

    bool update();
    static bool modifierHeld();
};

} // namespace nightshift
