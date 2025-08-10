/**
 * @file listFiles.js
 * @brief MCP tool for listing files and directories
 * @author Native MCP Team
 * @date 2025
 * 
 * Provides a safe, read-only tool for listing files with glob patterns,
 * filtering, and sorting capabilities.
 */

import fg from 'fast-glob';
import { promises as fs } from 'node:fs';
import path from 'node:path';

/**
 * List files and directories in a workspace
 * 
 * @param {Object} args - Tool arguments
 * @param {string} args.base_path - Directory to start from, relative to workspace root
 * @param {string} [args.pattern='*'] - Glob pattern (minimatch syntax)
 * @param {boolean} [args.recursive=false] - Traverse subdirectories
 * @param {number} [args.max_depth=-1] - Limit directory depth (-1 for unlimited)
 * @param {boolean} [args.include_dirs=false] - Include directories in results
 * @param {boolean} [args.include_hidden=false] - Include dot-files/dirs
 * @param {number} [args.limit=1000] - Maximum entries to return
 * @param {string} [args.sort_by='name'] - Sort field: 'name', 'size', 'mtime'
 * @param {string} [args.order='asc'] - Sort order: 'asc' or 'desc'
 * @returns {Promise<{entries: Array, truncated: boolean}>} List results
 */
export async function listFiles(args) {
  const {
    base_path,
    pattern = '*',
    recursive = false,
    max_depth = -1,
    include_dirs = false,
    include_hidden = false,
    limit = 1000,
    sort_by = 'name',
    order = 'asc'
  } = args;

  // Validate inputs
  if (!base_path) {
    throw new Error('base_path is required');
  }

  if (limit < 1 || limit > 5000) {
    throw new Error('limit must be between 1 and 5000');
  }

  if (!['name', 'size', 'mtime'].includes(sort_by)) {
    throw new Error('sort_by must be one of: name, size, mtime');
  }

  if (!['asc', 'desc'].includes(order)) {
    throw new Error('order must be asc or desc');
  }

  // 1. Resolve to an absolute, workspace-scoped path
  const workspaceRoot = process.env.WORKSPACE_ROOT || process.cwd();
  const absBase = path.resolve(workspaceRoot, base_path);

  // Security: Prevent path traversal
  if (!absBase.startsWith(workspaceRoot)) {
    throw new Error('base_path outside workspace');
  }

  // Check if base path exists
  try {
    const baseStat = await fs.stat(absBase);
    if (!baseStat.isDirectory()) {
      throw new Error('base_path is not a directory');
    }
  } catch (err) {
    if (err.code === 'ENOENT') {
      throw new Error(`base_path does not exist: ${base_path}`);
    }
    throw err;
  }

  // 2. Build fast-glob options
  const depth = max_depth >= 0 
    ? max_depth 
    : recursive ? undefined : 0;

  // Configure glob options
  const globOptions = {
    cwd: absBase,
    dot: include_hidden,
    onlyFiles: !include_dirs,
    onlyDirectories: false,
    deep: depth,
    absolute: true,
    followSymbolicLinks: false,  // Security: don't follow symlinks
    suppressErrors: true
  };

  // Handle special case: if we want both files and dirs
  let entries = [];
  
  if (include_dirs) {
    // Get both files and directories
    const files = await fg(pattern, { ...globOptions, onlyFiles: true });
    const dirs = await fg(pattern, { ...globOptions, onlyFiles: false, onlyDirectories: true });
    entries = [...files, ...dirs];
  } else {
    // Just files
    entries = await fg(pattern, globOptions);
  }

  // 3. Slice to limit before stat calls to save I/O
  const truncated = entries.length > limit;
  const sliced = entries.slice(0, limit);

  // Get metadata for each entry
  const withMeta = await Promise.all(
    sliced.map(async (absPath) => {
      try {
        const st = await fs.lstat(absPath);
        return {
          path: path.relative(workspaceRoot, absPath),
          type: st.isDirectory() ? 'dir' : 'file',
          size: st.isDirectory() ? 0 : st.size,
          mtime: st.mtime.toISOString()
        };
      } catch (err) {
        // File might have been deleted between glob and stat
        return null;
      }
    })
  );

  // Filter out nulls (failed stats)
  const validEntries = withMeta.filter(entry => entry !== null);

  // 4. Sort entries
  const factor = order === 'asc' ? 1 : -1;
  validEntries.sort((a, b) => {
    let valA, valB;
    
    if (sort_by === 'name') {
      valA = a.path.toLowerCase();
      valB = b.path.toLowerCase();
    } else if (sort_by === 'size') {
      valA = a.size;
      valB = b.size;
    } else if (sort_by === 'mtime') {
      valA = a.mtime;
      valB = b.mtime;
    }
    
    if (valA < valB) return -factor;
    if (valA > valB) return factor;
    return 0;
  });

  return {
    entries: validEntries,
    truncated
  };
}

/**
 * Tool definition for MCP
 */
export const LIST_FILES_TOOL = {
  name: 'list_files',
  description: 'List files and/or directories inside a workspace folder. Returns pathname, type, size (bytes) and ISO mtime for each entry.',
  parameters: {
    type: 'object',
    properties: {
      base_path: {
        type: 'string',
        description: "Directory to start from, relative to the workspace root. Use '.' for the root."
      },
      pattern: {
        type: 'string',
        description: "Optional glob (minimatch syntax) evaluated relative to base_path, e.g. '**/*.js'. Defaults to '*' (everything).",
        default: '*'
      },
      recursive: {
        type: 'boolean',
        description: 'If true traverse sub-directories. Ignored when max_depth > 0; use max_depth instead for finer control.',
        default: false
      },
      max_depth: {
        type: 'integer',
        description: 'Limit directory depth relative to base_path. 0 means only the base directory. Use -1 for unlimited.',
        default: -1
      },
      include_dirs: {
        type: 'boolean',
        description: 'Include directory entries in the result (not just files).',
        default: false
      },
      include_hidden: {
        type: 'boolean',
        description: 'Include dot-files / dot-dirs.',
        default: false
      },
      limit: {
        type: 'integer',
        description: 'Maximum number of entries to return. Use to avoid huge outputs.',
        default: 1000,
        minimum: 1,
        maximum: 5000
      },
      sort_by: {
        type: 'string',
        description: "Field to sort on: 'name', 'size', 'mtime'.",
        enum: ['name', 'size', 'mtime'],
        default: 'name'
      },
      order: {
        type: 'string',
        description: "'asc' or 'desc' sort order.",
        enum: ['asc', 'desc'],
        default: 'asc'
      }
    },
    required: ['base_path']
  }
};