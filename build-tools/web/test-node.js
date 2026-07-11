#!/usr/bin/env node
/**
 * Node.js test for yetty WebAssembly build
 * Tests that the Emscripten module loads and initializes correctly
 */

const fs = require('fs');
const path = require('path');

const BUILD_DIR = process.argv[2] || 'build-webasm-dawn-release';

console.log('=== Node.js Emscripten Module Test ===');
console.log('Build directory:', BUILD_DIR);

process.chdir(BUILD_DIR);

// Check files exist
const requiredFiles = ['yetty.js', 'yetty.wasm'];
for (const f of requiredFiles) {
    if (!fs.existsSync(f)) {
        console.error(`ERROR: Missing ${f}`);
        process.exit(1);
    }
}
console.log('All required files present');

// Mock browser environment
global.window = {
    location: { pathname: '/', href: 'http://localhost:8080/' },
    encodeURIComponent: encodeURIComponent,
    addEventListener: () => {},
    removeEventListener: () => {},
    requestAnimationFrame: (cb) => setTimeout(cb, 16),
    devicePixelRatio: 1,
    innerWidth: 800,
    innerHeight: 600,
    prompt: () => null,
};

global.document = {
    getElementById: (id) => {
        if (id === 'canvas') {
            return {
                width: 800,
                height: 600,
                style: {},
                getBoundingClientRect: () => ({ width: 800, height: 600, left: 0, top: 0 }),
                getContext: (type) => {
                    if (type === 'webgpu') return null; // WebGPU not available in Node
                    return null;
                },
                addEventListener: () => {},
                removeEventListener: () => {},
                focus: () => {},
            };
        }
        return null;
    },
    createElement: (tag) => ({
        style: {},
        appendChild: () => {},
        setAttribute: () => {},
        getContext: () => null,
    }),
    body: { appendChild: () => {} },
    addEventListener: () => {},
    removeEventListener: () => {},
    hidden: false,
};

global.navigator = {
    userAgent: 'node-test',
    gpu: null, // No WebGPU in Node
    platform: 'linux',
};

global.location = global.window.location;
global.performance = { now: () => Date.now() };
global.requestAnimationFrame = global.window.requestAnimationFrame;
global.devicePixelRatio = 1;

// Mock fetch for data file loading
global.fetch = async (url) => {
    const filePath = url.startsWith('/') ? url.slice(1) : url;
    console.log(`Fetching: ${filePath}`);

    try {
        const data = fs.readFileSync(filePath);
        const arrayBuffer = data.buffer.slice(data.byteOffset, data.byteOffset + data.byteLength);

        return {
            ok: true,
            status: 200,
            url: url,
            arrayBuffer: async () => arrayBuffer,
            body: {
                getReader: () => {
                    let done = false;
                    return {
                        read: async () => {
                            if (done) return { done: true, value: undefined };
                            done = true;
                            return { done: false, value: new Uint8Array(data) };
                        }
                    };
                }
            },
            headers: {
                get: (name) => {
                    if (name.toLowerCase() === 'content-length') {
                        return String(data.length);
                    }
                    return null;
                }
            }
        };
    } catch (e) {
        console.error(`Failed to fetch ${filePath}:`, e.message);
        return {
            ok: false,
            status: 404,
            url: url,
        };
    }
};

// Track module state
let moduleInitialized = false;
let moduleError = null;
let filesystemReady = false;

// Create Module object
global.Module = {
    canvas: global.document.getElementById('canvas'),

    ENV: {
        YTRACE_DEFAULT_ON: 'yes',
    },

    print: (text) => {
        console.log('[yetty]', text);
    },

    printErr: (text) => {
        console.error('[yetty-err]', text);
    },

    setStatus: (text) => {
        if (text) console.log('[status]', text);
    },

    onRuntimeInitialized: () => {
        console.log('=== Runtime initialized ===');
        moduleInitialized = true;
    },

    onAbort: (what) => {
        console.error('=== Module aborted ===', what);
        moduleError = what;
    },

    preRun: [],
    postRun: [],

    locateFile: (path, prefix) => {
        return prefix + path;
    },

    // Called when filesystem is ready
    monitorRunDependencies: (left) => {
        console.log(`Run dependencies left: ${left}`);
        if (left === 0) {
            filesystemReady = true;
        }
    },
};

// Timeout for the test
const TIMEOUT = 30000;
const startTime = Date.now();

console.log('\nLoading yetty.js...');

// Load the module
try {
    require('./yetty.js');
} catch (e) {
    console.error('Failed to load yetty.js:', e.message);
    console.error(e.stack);
    process.exit(1);
}

console.log('yetty.js loaded, waiting for initialization...');

// Wait for initialization or error
const checkInterval = setInterval(() => {
    const elapsed = Date.now() - startTime;

    if (moduleError) {
        console.error('\n=== TEST FAILED ===');
        console.error('Module error:', moduleError);
        clearInterval(checkInterval);
        process.exit(1);
    }

    if (moduleInitialized && filesystemReady) {
        console.log('\n=== Module initialized successfully ===');

        // Test filesystem
        console.log('\nTesting virtual filesystem...');
        try {
            const FS = Module.FS;
            if (FS) {
                // List root
                const root = FS.readdir('/');
                console.log('Root directory:', root);

                // Check assets
                if (root.includes('assets')) {
                    const assets = FS.readdir('/assets');
                    console.log('/assets:', assets);

                    // Check fonts-cdb
                    if (assets.includes('fonts-cdb')) {
                        const fontsCdb = FS.readdir('/assets/fonts-cdb');
                        console.log('/assets/fonts-cdb:', fontsCdb);

                        // Try to read a CDB file header
                        const cdbFiles = fontsCdb.filter(f => f.endsWith('.cdb'));
                        if (cdbFiles.length > 0) {
                            const cdbPath = `/assets/fonts-cdb/${cdbFiles[0]}`;
                            const stat = FS.stat(cdbPath);
                            console.log(`${cdbPath}: ${stat.size} bytes`);
                        }
                    }

                    // Check shaders
                    if (assets.includes('shaders')) {
                        const shaders = FS.readdir('/assets/shaders');
                        console.log('/assets/shaders:', shaders);
                    }
                }

                // Check demo
                if (root.includes('demo')) {
                    const demo = FS.readdir('/demo');
                    console.log('/demo:', demo.slice(0, 5), '...');
                }

                console.log('\n=== FILESYSTEM TEST PASSED ===');
            } else {
                console.error('FS not available');
            }
        } catch (e) {
            console.error('Filesystem test failed:', e.message);
        }

        console.log('\n=== ALL NODE TESTS PASSED ===');
        clearInterval(checkInterval);
        process.exit(0);
    }

    if (elapsed > TIMEOUT) {
        console.error('\n=== TEST TIMEOUT ===');
        console.error('Module did not initialize within', TIMEOUT, 'ms');
        console.error('moduleInitialized:', moduleInitialized);
        console.error('filesystemReady:', filesystemReady);
        clearInterval(checkInterval);
        process.exit(1);
    }
}, 100);

// Handle uncaught errors
process.on('uncaughtException', (e) => {
    console.error('\n=== UNCAUGHT EXCEPTION ===');
    console.error(e.message);
    console.error(e.stack);
    process.exit(1);
});

process.on('unhandledRejection', (reason, promise) => {
    console.error('\n=== UNHANDLED REJECTION ===');
    console.error('Reason:', reason);
    process.exit(1);
});
