![NIX — The Prompt Operating System](assets/brand/NIX%20prompt%20operating%20system%20banner.png)

# Why Nix?

[Back to the project](README.md) · [The NIX blueprint](https://core-nix.com/)

## Your computer should begin with what you want to do

You sit down at a computer with an intention. You want to write a book, return to a project, explore an idea, or play a game. Before you get to that intention, the computer usually asks you to translate it into its own organization: choose an application, find a file, navigate a desktop, configure a workspace, and remember which tools fit together.

NIX starts with a different question: what would you like to do?

The idea is simple enough to describe in one sentence, but it reaches into the whole operating system. If intent is the starting point, the interface, the way software is prepared, the way work is stored, and the relationship between intelligence and control all need to support that starting point. A prompt becomes the front door to a system that can help assemble the right experience around your task.

NIX is a project by ZIA to explore that possibility on custom x64 computers. Core is the foundation being built underneath it. The ambition is an operating system that feels direct, personal, and capable: a place where you can express an outcome, understand the proposed path, and stay in control as the computer helps you carry it through.

The project is early. This page tells the story of the intended experience and the conceptual phases that could bring it into existence. The repository already contains a small bootable Core prototype. The broader intelligence, isolation, compatibility, and hardware capabilities described here remain work ahead.

## The prompt is home

Imagine starting a session with a quiet screen and an invitation to say what you need. There is no requirement to organize a desktop before you can begin. You might say that you want to write a book. NIX could ask whether you are starting from scratch or continuing a manuscript. If you already have material, the next useful step could be choosing the work you want to bring into the session. If you are starting fresh, it could be creating a title and a first chapter.

That conversation should lead somewhere tangible. A writing task calls for a writing interface: a manuscript, room for notes, a way to move between chapters, and clear save and export actions. A task that needs a visual workspace should receive one. A task that needs a precise command environment should be able to open one. The prompt introduces the work; the system then presents the controls that help you do it.

This is what “prompt first” means for NIX. A person can describe an outcome before choosing all the machinery underneath it. They can also choose that machinery directly when they know what they want. A desktop can be requested. A terminal can be requested. Neither needs to be a mandatory stop on every journey through the computer.

That flexibility matters because a good interface changes with the task. Writing, reviewing a plan, managing a game installation, and recovering a saved document require different kinds of attention. NIX aims to make those transitions feel coherent while keeping the important actions visible and understandable.

## From an intention to a usable experience

For NIX to earn its place as an operating system, interpreting a request is only the beginning. The useful part is the path from a request to a working environment.

Consider the book example. The system needs to understand enough about the request to ask relevant questions. It needs to know which writing capabilities are already available. It needs to identify the material you have chosen to use, prepare an appropriate workspace, and make it clear where new writing will be saved. If something is missing, it needs to explain the dependency and the proposed next step.

The intended approach is research, reuse, then build. A maintained, suitable tool should be considered before inventing another one. Existing software contains years of work, decisions, and testing. NIX should learn how to put that work to use where it fits. A new interface or a newly built component becomes an option when the existing choices leave a meaningful gap.

The person using the system should be able to follow that reasoning. A plan should explain what will be prepared, what it needs access to, whether it needs a network connection, and what will change. When a choice affects personal work or installs software, the choice should be concrete enough to review. Progress, cancellation, failure, and recovery belong in the experience from the start.

The long-term aim is continuity. The request, the plan, the workspace, and the saved result should feel like parts of the same activity. You should be able to return later and continue from your work, with enough context to understand how the environment was assembled.

## Local intelligence, with a clear role

NIX's proposed intelligence runs on the computer. The goal is to make understanding, planning, and assistance available through a local model rather than requiring a remote model service for ordinary use. That choice makes the machine's own resources part of the product: available memory, processor capability, supported acceleration, and the cost of running a model all influence the experience.

Local intelligence can help interpret a request, ask a useful question, suggest a plan, or find relevant material within the work you have selected. It can also make mistakes. The operating system must account for that possibility in its design.

The planned division of responsibility gives the model room to propose while deterministic system services decide what an approved operation is allowed to do. A fluent answer should never be enough to grant access to unrelated documents, install unreviewed software, or change the trusted foundation of the machine. Permissions need to remain explicit and enforceable outside the model.

The same distinction applies to online research. Local inference describes where the intelligence runs. A particular research task may still need information from the internet. The proposed experience keeps those decisions separate: the person can understand why a task needs a connection and decide whether to allow it. Working offline should remain a meaningful mode of operation.

The current Core MVP contains a deterministic intent router. It does not include a local model or an operational AI agent. A usable system during model failure is part of the intended design, and the prototype begins by proving that basic interaction and saved work can exist independently of inference.

## Memories make the work the center

NIX calls the things you keep Memories: a manuscript, a photograph, a recording, a project, an installation, or a game save. The name expresses a product idea. Your work should remain recognizable as your work as you move between tasks and tools.

In the proposed system, a Memory has an identity that can persist across changes to its presentation. It can carry a name, a kind of content, a history, and a record of where it came from. A writing workspace should be able to continue the same manuscript, show earlier versions, and export the result in an ordinary form you can keep elsewhere.

Access is an equally important part of the concept. Opening one manuscript should give a writing tool the material it needs for that task. It should not automatically make every document on the computer available to the tool or the model. Choosing which Memories belong in a session is a natural point for a person to understand and control that access.

The word also needs clear boundaries. A saved novel and the context an assistant remembers about a conversation are different kinds of information. Installed game assets and the progress you made playing the game have different lifetimes. Removing an application should leave a separate, understandable decision about personal creations and saved progress.

The prototype starts with a very small version of this idea: named manuscripts, explicit saves, recovery from a previous committed version, and export to ordinary text files. A richer Memory system will need more kinds of content, broader persistence services, access controls, backup, and recovery choices. The principle remains the same: completing a task includes preserving the work in a form the person can return to and take with them.

## Pods give a task an environment

Different tasks need different software environments. A native writing tool, a legacy game, and a modern application may have very different expectations about the system beneath them. NIX uses the concept of a Pod to describe an application environment together with its software, permissions, selected data, and resource needs.

For a person using NIX, the useful question is whether a task can be prepared reliably and whether its boundaries are understandable. The underlying route could eventually be a native application, a compatible runtime, an emulated environment, or a virtual machine. Those routes have different requirements and different costs. They need to be chosen and tested for the particular application.

A game illustrates why this matters. Saying that you want to play a game is enough to begin a conversation, but it does not settle which edition you own, how it is installed, which graphics capabilities it requires, or whether its online services will work. A responsible system needs those details before it can describe a credible plan. Licenses, supported hardware, input, audio, graphics, and application behavior all remain part of making the result work.

The Pod concept aims to organize that complexity into an environment you can inspect, manage, and eventually replace or roll back. It also aims to give failures a boundary, so a problem in one task does not become a problem for the whole system. Those isolation properties require real operating-system mechanisms underneath them.

The current Author workspace is built into Core. It is not yet an isolated Pod. The broader Pod model is a design direction whose usefulness will be established through working, tested environments rather than a promise of universal application compatibility.

## Ghost belongs here

A prompt-first operating system should still make room for the people who want direct commands, repeatable actions, and a precise view of what the computer is doing. Ghost is NIX's default terminal when a person requests one.

Ghost brings an established visual direction to that part of the experience: a light canvas, a focused toolbar, readable monospace text, and an interface that leaves room for the work. Its place in NIX follows the same principle as the rest of the system. The terminal is available because it is useful for some tasks and preferred by some people. Opening it is a deliberate choice.

The Core edition begins with a port of a limited command engine connected to Memories. It can browse the available writing, read a manuscript, open it in Author, and append text through the persistent store. Quoting and command chains make these actions more expressive without pretending that all the facilities of a mature operating system already exist.

The original macOS Ghost depends on services that Core does not yet provide. Bringing its broader capabilities into NIX will require a deeper platform beneath it, including process execution, terminal services, and richer application interfaces. The project retains the upstream source and the origin of the current port so that this relationship is visible as the system develops.

## Core is the foundation that has to earn trust

NIX is the experience people interact with. Core is the operating-system foundation that must make that experience dependable.

The architectural direction is a small foundation with carefully bounded services around it. Over time, Core needs to establish how memory is protected, how tasks run, how services communicate, how devices are accessed, and how one component can fail without taking the rest of the system with it. Those responsibilities become more important as the system begins handling valuable work and running software assembled for different purposes.

The current prototype makes a narrower claim. It boots a freestanding kernel in QEMU, presents a framebuffer interface, accepts keyboard input, supports a small writing experience, and saves to an attached virtual disk. The work runs inside an early kernel with limited services and no process isolation. It is a place to begin proving the interaction and persistence model.

That distinction gives the project a useful discipline. A concept can describe the destination, while an implementation must show what it can actually do today. Booting, saving, restarting, and recovering are modest experiences compared with the full vision, but they are concrete. Each creates a stronger basis for the next layer.

## Built for people who choose their own computers

NIX's intended hardware direction is custom x64 computers. There is a natural connection between choosing the parts of a machine and wanting more say in how that machine behaves. People who build computers already understand that capabilities come from real components, supported interfaces, and tradeoffs.

That audience includes someone planning a first build and someone who has spent years refining a workstation. Both can recognize the appeal of an operating system that begins with their purpose and gives them room to understand the choices underneath it.

The hardware plan must become specific as the project grows. A narrow, selected target makes it possible to test display, input, storage, networking, and recovery as a coherent whole. Wider support can grow from evidence gathered on configurations the project can actually validate. Graphics acceleration, virtualization, games, and immersive devices each add their own engineering demands.

QEMU supplies a controlled starting environment for this work. The MVP can run there on an Intel Mac while the project explores its basic interactions. Running in that environment does not establish support for the Mac's physical devices or for every custom PC. It establishes a repeatable place to develop and test the first slice.

## The project phases are a sequence of questions

The phases below describe concepts and the experiences each is intended to prove. They are not release dates or commitments that every idea will ship unchanged. Results from one phase should help shape the next.

### Establish a dependable beginning

The first question is whether the foundation can start predictably and remain understandable when something goes wrong. A person needs a way into the system, working input, useful feedback, and a path to preserve or recover work.

The present QEMU prototype belongs to this beginning. It makes the first interaction visible and gives the project something real to restart, inspect, and improve. Further foundation work needs to turn those beginnings into stronger memory, execution, storage, and recovery services.

### Prove one complete creative experience

Author is a deliberately focused first experience. Writing a book brings together an intention, a workspace, a named piece of work, a save operation, and a return to that work later. It gives the project a meaningful way to ask whether the system helps a person complete an activity.

A more capable Author experience could add structure, revision, notes, and import and export choices. The important test is continuity: can someone begin, make progress, leave, and return with confidence that their work is still there?

### Introduce local understanding

The next conceptual step is to make more requests understandable through local intelligence. The system should be able to ask a relevant question, propose an appropriate experience, and handle uncertainty openly.

That phase also needs to prove the boundaries around inference. A model should receive the context selected for its task, operate within the machine's resource limits, and leave the basic system usable if it stops responding or produces an unsuitable suggestion. Better understanding has to arrive alongside dependable control.

### Prepare software through reviewed plans

As the range of tasks expands, NIX needs a repeatable way to assess available software and prepare environments. The research, reuse, then build approach becomes more consequential here: where did a component come from, why was it selected, what does it need, and how can a person reverse the change?

This phase is about turning assistance into accountable preparation. A working result includes a clear plan, bounded access, a tested environment, and a way back when the result does not meet the need. Generated software, where appropriate, must fit within those same expectations.

### Validate compatibility one experience at a time

Running a selected legacy or external application would broaden what NIX can help someone do. The useful achievement is a particular application working in a particular environment with known requirements and known limits.

The experience should include preparation, installation from appropriate media, access to the selected Memories, and a clear management view. It should also make failure understandable. Compatibility becomes a collection of tested outcomes that people can rely on when their situation matches the evidence.

### Grow into supported hardware and richer experiences

The later direction includes validated physical computers, stronger graphics and compute support, more demanding applications, and potentially gaming and immersive experiences. These capabilities depend on drivers, performance, input, audio, display timing, and hardware support that each need their own work.

The conceptual goal is to preserve the same clarity as the system grows. A richer computer should still begin with a person's intention. More power should make more outcomes possible while keeping the path to those outcomes understandable.

## What success would feel like

Success begins with a practical experience. You ask to continue a book, and the right work is available. You understand what a proposed installation needs before agreeing to it. You can choose Ghost when you want direct commands. You can return after a restart and find what you saved. If a tool fails, there is a clear next step and a way to recover.

As the project matures, those moments should connect into a relationship with the computer that feels consistent. The interface adapts to the activity. Personal work stays recognizable. Intelligence offers help within boundaries you can understand. The underlying system carries out approved operations and makes its actual capabilities clear.

NIX is being built toward that relationship, beginning with a small Core and a working first slice. The vision gives the work a direction. Each useful, repeatable experience gives it substance.

Your build. Your work. Your intention as the starting point.

---

[Explore the current MVP](README.md) · [View the project](https://github.com/caraveo/nix) · [NIX blueprint](https://core-nix.com/)
