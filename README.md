## Focus Menu
*An Xfce program-switching applet based on classical standards*
### What’s the purpose?
I wanted to have a menu on the top-right of the screen that would allow me to switch programs, just as in Classic Mac OS. I found that for some reason, there aren’t really Linux applets that work like this. There are window-switching menus, but they have differences that don’t make them great for the purpose of replicating traditional behaviour.
### How does it work?
After you add the applet to Xfce4 Panel for the first time, the icon and name of whatever program you have running will appear somewhere on the panel. (Suppose, for these examples, that that is Mousepad.) After clicking on it, a drop-down menu with the following options will come up:

*Hide Mousepad*:

Every window in Mousepad is minimized at once. Focus will fall back to whatever program is below. Mousepad’s icon will be slightly transparent, and its name in italics, which both indicate it is hidden.

*Hide Others*:

Every other program is hidden.

*Show All*:

All programs with their windows are unhidden.

----- 
After the separator, the program list follows. There’s a menu item for every program in the workspace, plus one special case: if the file manager is not open, then the desktop manager (Xfdesktop, or Caja) will have an entry here. It is the only item that can’t be hidden, and so it’s underlined to show its special status. The program list is arranged in alphabetical order. The focused program is denoted with a symbol to the left of its icon.
### Are there preferences to set?
Yes, three. Right-click the applet and open Properties, and you’ll see:

**• Use submenus for multi-window applications**

The default classical behaviour is to load every window associated with the application. But if you want more window-centered behaviour, check this option to load a window-switcher mode. Single-window applications will be dealt with normally. Applications with multiple windows gain submenus, which contain an option to “show all x’s windows”, and then, in a list below a separator, a menu item for each window, arranged in alphabetical order. Selecting a window item will bring up only that window, not the rest of the program.

**• Icon only (hide application name)**

The panel button has only an icon. (The menu items are unchanged.)

**• Use checkmarks instead of radio buttons**

The Classic application switcher used a checkmark to denote the active application. Technically, this slightly departs from the modern Linux standard, which uses radio items for mutually exclusive options. This brings back the vintage aesthetic for those who appreciate it.
### What’s compatibility like?
This applet officially supports two combinations of programs, for the purpose of determining the desktop manager: Xfce’s default Xfdesktop as desktop manager and Thunar as the file manager, and alternatively, Caja as desktop and file manager. The code also includes checks for Nemo, but that’s presently untested and not officially supported. If you try it out, let me know how it goes. Support for other desktop managers is not implemented.

Xfce, and also MATE, at this time, are designed to work primarily with X11. Thus this applet’s window management features are also designed to work with X11, via wnck. It does not support and is not tested with Wayland, and there is no reason to think it would work at all. (If it somehow does, it will not be because of anything that I did.)

I’ve built and run it on Fedora 42 and Debian 12. It might well work on other distributions, provided they have the appropriate dependencies. If you use another distro and are especially interested in getting this, but the build doesn’t work and you don’t have the experience to tweak yourself, let me know and I’ll see if I can help.
### How do I install it?
The way I’ve done it is to extract the files into a new folder, and in the terminal, enter “make && sudo make install”, to install the applet to the root system. Then you can restart the panel (perhaps with xfce4-panel -r), and it should come up in “Add New Items”.
I hope to get .deb and .rpm packages out, at my convenience.
### Are there any other features?
There’s one thing. As mentioned before, program and window names are obtained with the help of wnck, a window monitor. Normally, many of them are ugly. I’ve included a feature which processes names and attempts to make them look ‘pretty’, following the naming conventions they’d have if they were programs running on Classic Mac OS.
### How is this different from what’s already out there?
The stock Xfce “Window Menu” applet is the closest competitor, though MATE and Cinnamon have their own equivalent applets (MATE’s is clearly worse, Cinnamon’s is comparable but lacks the button icon). Here’s a few (though not an exhaustive list) of differences:

• Window Menu doesn’t have the name of the program in its button, but only the icon. (If you have Arrow mode turned on, it doesn’t have the icon either.)

• Window Menu shows every individual window, not programs.

• Window Menu sorts windows by which ones were last accessed, not alphabetically.

• Window Menu's minimized window items have transparent icons, but the text remains the same.

• Window Menu doesn’t offer the option to minimize anything, or to show multiple minimized windows at once.

• Window Menu uses bold and italicized text to show the currently focused window, not radio items (or checkboxes).

• Window Menu offers the option of listing windows in different workspaces, and filtering or managing these workspaces. Focus Menu only manages the programs that are visible in one space at a time.
### When would I use this over the competition?
You will probably prefer this applet if you like alphabetical order, accessing windows by programs, or the ability to hide/reveal things based on menu options. You might prefer this applet if you like prettier or more elegant things; it is more so than its competition. Finally, if you are a fan of Classic Macintoshes and want something that reproduces that functionality, this is your only option, that I’m aware of, that works with a mainstream desktop environment.
### When would I not prefer this?
I expect it’s less useful if you’re big on workspaces and want something that helps manage them. I don’t use workspaces, and I designed an applet that ignores them as much as possible.
*(I think the model this is re-implementing, is, for better or worse, something of an alternative to workspaces. Accessing and hiding programs and all their windows, en masse, allows users to switch focus on tasks in a similar way to how workspaces have been advertised, in a way that I suspect is a little more accessible for many users. If you’ve never tried it before, you might give it a shot and see if you like it.)*
If you used Classic Macs before and hated the application switcher menu for some reason, this applet shouldn’t do anything that will make you like it any better.
### Is there anything I should be concerned about?
There’re two things some users would probably like to know.

First, it might seem obvious given the applet’s job description, but yes, this applet is watching you. Usually it leans on wnck to detect what’s up (which is why there’s no Wayland support), but occasionally it will look directly at the proc directory for things like the desktop manager. If that bothers you, you’re free to look through the code and verify that there are no network connections or anything else being done that is under-board.

-----

The second is that I’m not a C programmer. I’ve done work in other languages, but I’m a C and GTK novice, at best. The bulk of this applet’s code was produced by an AI tool, namely Anthropic’s Claude (usually the Sonnet 4 model).

This was not a mindless process. I had specific goals in mind, gave prompts with achieving those goals in mind, rejected or threw out suggestions, and made other tweaks and changes where I determined the bot missed the mark. I worked through build errors and debugged logical problems that the bot missed, on multiple occasions. I have personally evaluated the functionality of the applet to make sure it does what I intended. I was the manager, project lead, designer, and tester, and I was a programmer who inspected, tweaked and rearranged the code.

But even so, I technically didn’t originally write most of it. That may bother you. If it does, you are, again, free to test the applet and inspect the codebase yourself and see that it does what it ought to be doing. If you think there’s an area where the performance could be improved, make the tweaks in a fork and send a pull request. I will inspect it and think through whether the change should be made – the same as I did for the bot code – and accept it if it is correct.

Would it be nice if this applet had been made solely by human ability? Yes. But I don’t have the background in C or GTK’s libraries, or in dealing with xfconf or Xfce Panel’s expectations, and I don’t have the patience to take the time to learn all of them and work up to making this when this project has no apparent profit potential. If anyone else was going to make this, they would have done it by now. So the choice here isn’t between an applet with AI code and an applet by an elite human programmer, it’s a choice between an applet and no applet.

### Are there any known bugs or issues?
Xfce Notifyd can appear as a program sometimes, until it is dismissed.
Other than that, there's nothing I'm presently aware of. If you find anything, let me know.
### Why the license?
Although GPL v2 isn’t my favorite, culture and the ecosystem seemed to lend themselves to it; it is popular among Xfce applets. The decision to use AI tools also contributed, given that I’m not entirely sure what its sources were, and that given the project context, it seems possible at least some of it could have originally been inspired by GPL code. I feel much of the code that I have reviewed wouldn’t be reasonably implemented in any other way, but it’s better to be safe here.
This is not a great inconvenience, given the applet’s purpose and audience. Given its intimate access to the system, free access to the source code should help maintain user trust, and I feel all Classic Mac fans should have access to these features if they want them.
