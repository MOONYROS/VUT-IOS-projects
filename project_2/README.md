# IOS project 2

## Assignment

Assignment was to create a program using parallel programming that will create water molecules which means using two main queues:

- Hydrogen (H)
- Oxygen (O)

To achieve good results, semaphores and locks had to be used, or the program would create wrong number of molecules. The trick was to create a program that would not run into deadlock (the processes lock each other out) or any data critial situation whatsoever.
Goal of this project was to get an idea on how parallel programming works and where it could be used and to know its advantages and disadvantages.

## How to run

To compile the program, you can use the **Makefile** using `./make` command in the folder with the Makefile in it.
To further run the program, you can use `./proj2 <oxygen number> <hydrogen number> <upper limit use> <upper limit molecule>` where:

- `<oxygen number>` is the amount of oxygen atoms,
- `<hydrogen number>`is the amount of hydrogen atoms,
- `<upper limit use>` is the upper time limit (in miliseconds) of how long an atom can stay unqueued,
- `<upper limit molecule>` is the upper time limit (in miliseconds) of how long it can take to create the molecule.

After executing the program, you can see on the standard output, how the program works and what's getting done.

## Tested environments

The script was tested on **Ubuntu 20.04**, **Ubuntu 22.04**, **Windows 10** and **Windows 10** with `gcc` compiler.
