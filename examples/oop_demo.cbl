// McBL# v2.0 — OOP / classInt Demo

// classInt style (spec asli)
inc(classInt_example);

    classInt(variableClass);
        #hello = "world"
        #world = "hello"

    classInt(printerClass);
        pr(hello)
        pr(world)

    printerClass()

endinc;

// Dev functions dengan parameter langsung
inc(oop_basic);

    dev speak(name, sound);
        pr(name + " says: " + sound)

    dev describe(name);
        pr("I am: " + name)

    speak("Rex", "Woof!")
    speak("Whiskers", "Meow~")
    describe("Rex")

    pr("OOP demo done")

endinc;
