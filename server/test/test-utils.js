/**
 * Simple test utilities for script-based testing
 * Returns exit codes: 0 for pass, 1 for fail
 */

class TestRunner {
  constructor(name) {
    this.name = name;
    this.passed = 0;
    this.failed = 0;
    this.currentTest = null;
    this.failedTests = [];
  }

  test(description, fn) {
    this.currentTest = description;
    console.log(`  Testing: ${description}`);
    
    try {
      const result = fn();
      // Handle async tests
      if (result instanceof Promise) {
        return result
          .then(() => this.pass())
          .catch(err => this.fail(err));
      }
      this.pass();
    } catch (err) {
      this.fail(err);
    }
  }

  async testAsync(description, fn) {
    this.currentTest = description;
    console.log(`  Testing: ${description}`);
    
    try {
      await fn();
      this.pass();
    } catch (err) {
      this.fail(err);
    }
  }

  pass() {
    this.passed++;
    console.log(`    ✓ PASS`);
  }

  fail(err) {
    this.failed++;
    this.failedTests.push({
      test: this.currentTest,
      error: err.message || err
    });
    console.log(`    ✗ FAIL: ${err.message || err}`);
    if (err.stack && process.env.DEBUG) {
      console.log(err.stack);
    }
  }

  summary() {
    console.log('\n' + '='.repeat(50));
    console.log(`Test Suite: ${this.name}`);
    console.log(`Passed: ${this.passed}`);
    console.log(`Failed: ${this.failed}`);
    
    if (this.failed > 0) {
      console.log('\nFailed tests:');
      this.failedTests.forEach(({ test, error }) => {
        console.log(`  - ${test}: ${error}`);
      });
    }
    
    console.log('='.repeat(50) + '\n');
    
    // Return exit code
    return this.failed > 0 ? 1 : 0;
  }
}

/**
 * Simple assertion functions
 */
const assert = {
  equal(actual, expected, message) {
    if (actual !== expected) {
      throw new Error(message || `Expected ${expected}, got ${actual}`);
    }
  },

  deepEqual(actual, expected, message) {
    if (JSON.stringify(actual) !== JSON.stringify(expected)) {
      throw new Error(message || `Objects not equal:\nExpected: ${JSON.stringify(expected)}\nActual: ${JSON.stringify(actual)}`);
    }
  },

  ok(value, message) {
    if (!value) {
      throw new Error(message || `Expected truthy value, got ${value}`);
    }
  },

  throws(fn, message) {
    let threw = false;
    try {
      fn();
    } catch (err) {
      threw = true;
    }
    if (!threw) {
      throw new Error(message || 'Expected function to throw');
    }
  },

  async rejects(asyncFn, message) {
    let threw = false;
    try {
      await asyncFn();
    } catch (err) {
      threw = true;
    }
    if (!threw) {
      throw new Error(message || 'Expected async function to reject');
    }
  },

  includes(str, substring, message) {
    if (!str.includes(substring)) {
      throw new Error(message || `Expected "${str}" to include "${substring}"`);
    }
  }
};

/**
 * Wait for a condition to be true
 */
async function waitFor(conditionFn, timeout = 5000, interval = 100) {
  const start = Date.now();
  
  while (Date.now() - start < timeout) {
    if (await conditionFn()) {
      return true;
    }
    await new Promise(resolve => setTimeout(resolve, interval));
  }
  
  throw new Error(`Timeout waiting for condition after ${timeout}ms`);
}

/**
 * Sleep for a given time
 */
function sleep(ms) {
  return new Promise(resolve => setTimeout(resolve, ms));
}

export { TestRunner, assert, waitFor, sleep };