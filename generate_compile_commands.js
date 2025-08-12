#!/usr/bin/env node

import fs from 'fs';
import path from 'path';
import { fileURLToPath } from 'url';

const __dirname = path.dirname(fileURLToPath(import.meta.url));

// Read the make output
const makeOutput = fs.readFileSync('make_output.txt', 'utf8');
const lines = makeOutput.split('\n');

const compileCommands = [];
const workspaceRoot = process.cwd();

// Parse each line looking for compilation commands
lines.forEach(line => {
  line = line.trim();
  
  // Look for c++ or clang++ compilation commands
  if ((line.startsWith('c++') || line.startsWith('clang++') || line.startsWith('g++')) && 
      !line.includes(' -o bin/') && !line.includes(' -o libheadless.a')) {
    // Skip linking commands, only want compilation
    return;
  }
  
  if ((line.startsWith('c++') || line.startsWith('clang++') || line.startsWith('g++')) && 
      line.includes('.cpp')) {
    
    // Extract source files from the command
    const parts = line.split(/\s+/);
    const compiler = parts[0];
    
    // Find all .cpp files in the command
    const sourceFiles = parts.filter(part => part.endsWith('.cpp'));
    
    // Find the output file (-o flag)
    const outputIndex = parts.indexOf('-o');
    const outputFile = outputIndex !== -1 ? parts[outputIndex + 1] : null;
    
    // Get all flags (everything that starts with -)
    const flags = parts.filter(part => 
      part.startsWith('-') && 
      part !== '-o' && 
      part !== '-c' &&
      !part.startsWith('-L') && // Skip linker flags
      !part.startsWith('-l')    // Skip library flags
    );
    
    // For each source file, create a compile command entry
    sourceFiles.forEach(sourceFile => {
      // Convert relative paths to absolute
      let absoluteSourceFile;
      if (sourceFile.startsWith('../')) {
        absoluteSourceFile = path.join(workspaceRoot, sourceFile);
      } else {
        absoluteSourceFile = path.join(workspaceRoot, 'src', sourceFile);
      }
      
      // Build the compilation command
      const command = [
        compiler,
        ...flags,
        '-c', // Add -c flag for compilation only
        sourceFile
      ].join(' ');
      
      compileCommands.push({
        directory: path.join(workspaceRoot, 'src'),
        command: command,
        file: absoluteSourceFile
      });
    });
  }
});

// Also handle the libheadless.a compilation which uses -c flag
lines.forEach(line => {
  line = line.trim();
  
  if (line.includes('-c') && line.includes('.cpp')) {
    const parts = line.split(/\s+/);
    const compiler = parts[0];
    
    // Find all .cpp files
    const sourceFiles = parts.filter(part => part.endsWith('.cpp'));
    
    // Get all flags
    const flags = parts.filter(part => 
      part.startsWith('-') && 
      part !== '-c' &&
      !part.startsWith('-L') &&
      !part.startsWith('-l')
    );
    
    sourceFiles.forEach(sourceFile => {
      let absoluteSourceFile;
      if (sourceFile.startsWith('../')) {
        absoluteSourceFile = path.join(workspaceRoot, sourceFile);
      } else {
        absoluteSourceFile = path.join(workspaceRoot, 'src', sourceFile);
      }
      
      const command = [
        compiler,
        ...flags,
        '-c',
        sourceFile
      ].join(' ');
      
      // Check if we already have this file
      const exists = compileCommands.some(cmd => cmd.file === absoluteSourceFile);
      if (!exists) {
        compileCommands.push({
          directory: path.join(workspaceRoot, 'src'),
          command: command,
          file: absoluteSourceFile
        });
      }
    });
  }
});

// Write compile_commands.json
fs.writeFileSync('compile_commands.json', JSON.stringify(compileCommands, null, 2));
console.log(`Generated compile_commands.json with ${compileCommands.length} entries`);