# Editor - Lightweight Terminal Text Editor

**Editor** is an extremely simple-to-use terminal text editor. Written in C, it offers simplicity while being very responsive and performant. Editor was inspired by and is a derivative of [antirez's kilo editor](https://viewsourcecode.org/snaptoken/kilo/index.html).  

## Demo Video

<video src="https://github.com/user-attachments/assets/cbc12382-6372-4dbe-aefa-130904038f1e"></video>

## Installation

Editor is currently in alpha phase, and no precompiled binary is available. Any contributions from you to compile and package binaries will be appreciated.

### Building From Source

#### Dependencies

* C Compiler linked to your `PATH`
* CMake version 3.23 or higher
* Make
* Git

#### Linux / macOS (Unix)

```bash
# Fetch repository
git clone https://github.com/111nation/Editor

# Build Editor
cd Editor
cmake -S . -B build/
cmake --build build
```

To run Editor enter the build folder and execute Editor

```bash
cd build
./editor
```

#### Windows

Contributors are needed to test and write the Windows build process!

### Getting Started

#### Keybinds 

<table>
	<tr>
		<th width="150px">Keys</th>
		<th width="300px">Description</th>
	</tr>
	<tr>
		<td>
			Arrow Keys
		</td>
		<td>
			Cursor movement controls.
		</td>
	</tr>
	<tr>
		<td>
			<kbd>Ctrl</kbd> + <kbd>S</kbd>
		</td>
		<td>
			Save
		</td>
	</tr>
	<tr>
		<td>
			<kbd>Ctrl</kbd> + <kbd>Q</kbd>
		</td>
		<td>
			Quit Editor. <br/><br/><b>Note:</b> You may have to press <kbd>Ctrl</kbd> + <kbd>Q</kbd> again to force quit without saving.
		</td>
	</tr>
</table>

#### Creating New File

Opening Editor without any file arguments opens a blank file to write into. 

```bash
./editor
```
Editor will display the welcome message and also allow you to start typing immediately :)

Write your text and save using <kbd>Ctrl</kbd> + <kbd>S</kbd>. Editor will prompt you for a file name and location to enter. <b>Note:</b> File path and file name are path-relative.

#### Open Existing File

Editor can open existing files in a breeze. Just specify the file to open.

```bash
  ./editor ../src/main.rs
```

