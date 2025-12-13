import fs from 'fs';
import path from 'path';
import { fileURLToPath } from 'url';
import { spawnSync } from 'child_process';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const ROOT_DIR = path.resolve(__dirname, '..');

const SKIP_DIR_NAMES = new Set([
    '.git',
    '.direnv',
    '.venv',
    '.documentation',
    '.mypy_cache',
    '.pytest_cache',
    '.ruff_cache',
    '.cache',
    'result',
    'node_modules',
]);

const SKIP_FILE_NAMES = new Set([
    'LICENSE',
    'flake.lock',
]);

const SKIP_PATH_PREFIXES = [
    `src${path.sep}wlr-layer-shell-unstable-v1-`,
    `src${path.sep}xdg-shell-protocol.`,
];

function is_skipped_path(relativePath) {
    for (const prefix of SKIP_PATH_PREFIXES) {
        if (relativePath.startsWith(prefix))
            return true;
    }
    return false;
}

function is_text_extension(ext, baseName) {
    if (baseName === 'Makefile')
        return false;
    if (baseName.endsWith('.mk'))
        return false;

    const textExts = new Set([
        '.c',
        '.h',
        '.frag',
        '.vert',
        '.glsl',
        '.slang',
        '.slangp',
        '.glslp',
        '.nix',
        '.js',
        '.mjs',
        '.json',
    ]);
    return textExts.has(ext);
}

function strip_c_like_comments(input) {
    const CODE = 0;
    const LINE = 1;
    const BLOCK = 2;
    const DQ = 3;
    const SQ = 4;
    const BT = 5;

    let state = CODE;
    let out = '';
    let i = 0;
    let lineHasNonWs = false;
    let lineHadComment = false;
    let lineBuf = '';
    let lastEmittedBlank = false;
    let hasEmittedNonBlank = false;

    function emit_char(c) {
        lineBuf += c;
        if (c !== ' ' && c !== '\t' && c !== '\r')
            lineHasNonWs = true;
    }

    function emit_newline(preserveBlank) {
        const keepLine = lineHasNonWs || !lineHadComment;
        if (keepLine) {
            const isBlank = lineBuf.trim().length === 0;
            if (isBlank && !preserveBlank) {
                if (hasEmittedNonBlank && !lastEmittedBlank) {
                    out += '\n';
                    lastEmittedBlank = true;
                }
            } else {
                out += `${lineBuf}\n`;
                hasEmittedNonBlank = true;
                lastEmittedBlank = false;
            }
        }
        lineHasNonWs = false;
        lineHadComment = false;
        lineBuf = '';
    }

    function emit_eof(preserveBlank) {
        if (lineBuf.length === 0)
            return;
        const keepLine = lineHasNonWs || !lineHadComment;
        if (keepLine) {
            const isBlank = lineBuf.trim().length === 0;
            if (isBlank && !preserveBlank) {
                if (hasEmittedNonBlank && !lastEmittedBlank)
                    out += '\n';
            } else {
                out += lineBuf;
            }
        }
        lineHasNonWs = false;
        lineHadComment = false;
        lineBuf = '';
    }

    while (i < input.length) {
        const ch = input[i];
        const next = i + 1 < input.length ? input[i + 1] : '';

        if (state === CODE) {
            if (ch === '"') {
                state = DQ;
                emit_char(ch);
                i++;
                continue;
            }
            if (ch === "'") {
                state = SQ;
                emit_char(ch);
                i++;
                continue;
            }
            if (ch === '`') {
                state = BT;
                emit_char(ch);
                i++;
                continue;
            }
            if (ch === '/' && next === '/') {
                state = LINE;
                lineHadComment = true;
                i += 2;
                continue;
            }
            if (ch === '/' && next === '*') {
                state = BLOCK;
                lineHadComment = true;
                i += 2;
                continue;
            }
            if (ch === '\n') {
                emit_newline(false);
                i++;
                continue;
            }
            emit_char(ch);
            i++;
            continue;
        }

        if (state === LINE) {
            if (ch === '\n') {
                emit_newline(false);
                state = CODE;
            }
            i++;
            continue;
        }

        if (state === BLOCK) {
            if (ch === '\n') {
                emit_newline(false);
                i++;
                continue;
            }
            if (ch === '*' && next === '/') {
                state = CODE;
                i += 2;
                continue;
            }
            i++;
            continue;
        }

        if (state === DQ) {
            if (ch === '\n') {
                emit_newline(true);
                i++;
                continue;
            }
            emit_char(ch);
            if (ch === '\\' && next) {
                emit_char(next);
                i += 2;
                continue;
            }
            if (ch === '"')
                state = CODE;
            i++;
            continue;
        }

        if (state === SQ) {
            if (ch === '\n') {
                emit_newline(true);
                i++;
                continue;
            }
            emit_char(ch);
            if (ch === '\\' && next) {
                emit_char(next);
                i += 2;
                continue;
            }
            if (ch === "'")
                state = CODE;
            i++;
            continue;
        }

        if (state === BT) {
            if (ch === '\n') {
                emit_newline(true);
                i++;
                continue;
            }
            emit_char(ch);
            if (ch === '\\' && next) {
                emit_char(next);
                i += 2;
                continue;
            }
            if (ch === '`')
                state = CODE;
            i++;
            continue;
        }
    }

    emit_eof(false);
    return out;
}

function strip_nix_comments(input) {
    const CODE = 0;
    const LINE = 1;
    const BLOCK = 2;
    const DQ = 3;
    const IND = 4;

    let state = CODE;
    let out = '';
    let i = 0;
    let lineHasNonWs = false;
    let lineHadComment = false;
    let lineBuf = '';
    let lastEmittedBlank = false;
    let hasEmittedNonBlank = false;

    function emit_char(c) {
        lineBuf += c;
        if (c !== ' ' && c !== '\t' && c !== '\r')
            lineHasNonWs = true;
    }

    function emit_newline(preserveBlank) {
        const keepLine = lineHasNonWs || !lineHadComment;
        if (keepLine) {
            const isBlank = lineBuf.trim().length === 0;
            if (isBlank && !preserveBlank) {
                if (hasEmittedNonBlank && !lastEmittedBlank) {
                    out += '\n';
                    lastEmittedBlank = true;
                }
            } else {
                out += `${lineBuf}\n`;
                hasEmittedNonBlank = true;
                lastEmittedBlank = false;
            }
        }
        lineHasNonWs = false;
        lineHadComment = false;
        lineBuf = '';
    }

    function emit_eof(preserveBlank) {
        if (lineBuf.length === 0)
            return;
        const keepLine = lineHasNonWs || !lineHadComment;
        if (keepLine) {
            const isBlank = lineBuf.trim().length === 0;
            if (isBlank && !preserveBlank) {
                if (hasEmittedNonBlank && !lastEmittedBlank)
                    out += '\n';
            } else {
                out += lineBuf;
            }
        }
        lineHasNonWs = false;
        lineHadComment = false;
        lineBuf = '';
    }

    while (i < input.length) {
        const ch = input[i];
        const next = i + 1 < input.length ? input[i + 1] : '';

        if (state === CODE) {
            if (ch === '"') {
                state = DQ;
                emit_char(ch);
                i++;
                continue;
            }
            if (ch === "'" && next === "'") {
                state = IND;
                emit_char("'");
                emit_char("'");
                i += 2;
                continue;
            }
            if (ch === '#') {
                state = LINE;
                lineHadComment = true;
                i++;
                continue;
            }
            if (ch === '/' && next === '*') {
                state = BLOCK;
                lineHadComment = true;
                i += 2;
                continue;
            }
            if (ch === '\n') {
                emit_newline(false);
                i++;
                continue;
            }
            emit_char(ch);
            i++;
            continue;
        }

        if (state === LINE) {
            if (ch === '\n') {
                emit_newline(false);
                state = CODE;
            }
            i++;
            continue;
        }

        if (state === BLOCK) {
            if (ch === '\n') {
                emit_newline(false);
                i++;
                continue;
            }
            if (ch === '*' && next === '/') {
                state = CODE;
                i += 2;
                continue;
            }
            i++;
            continue;
        }

        if (state === DQ) {
            if (ch === '\n') {
                emit_newline(true);
                i++;
                continue;
            }
            emit_char(ch);
            if (ch === '\\' && next) {
                emit_char(next);
                i += 2;
                continue;
            }
            if (ch === '"')
                state = CODE;
            i++;
            continue;
        }

        if (state === IND) {
            if (ch === '\n') {
                emit_newline(true);
                i++;
                continue;
            }
            emit_char(ch);
            if (ch === "'" && next === "'") {
                emit_char(next);
                i += 2;
                state = CODE;
                continue;
            }
            i++;
            continue;
        }
    }

    emit_eof(false);
    return out;
}

function strip_comments_for_file(filePath, content) {
    const ext = path.extname(filePath);
    if (ext === '.nix')
        return strip_nix_comments(content);
    if (ext === '.json')
        return content;
    return strip_c_like_comments(content);
}

function walk_dir(dirAbs, dirRel, files) {
    const entries = fs.readdirSync(dirAbs, { withFileTypes: true });
    for (const ent of entries) {
        const nextRel = dirRel ? path.join(dirRel, ent.name) : ent.name;
        const nextAbs = path.join(dirAbs, ent.name);

        if (ent.isDirectory()) {
            if (SKIP_DIR_NAMES.has(ent.name))
                continue;
            walk_dir(nextAbs, nextRel, files);
            continue;
        }

        if (!ent.isFile())
            continue;

        if (SKIP_FILE_NAMES.has(ent.name))
            continue;

        if (is_skipped_path(nextRel))
            continue;

        const ext = path.extname(ent.name);
        if (!is_text_extension(ext, ent.name))
            continue;

        files.push({ abs: nextAbs, rel: nextRel });
    }
}

function normalize_newlines(s) {
    return s.replace(/\r\n/g, '\n');
}

function find_c_files_for_format() {
    const files = [];
    walk_dir(ROOT_DIR, '', files);
    return files
        .filter((f) => {
            const ext = path.extname(f.rel);
            return (ext === '.c' || ext === '.h') && f.rel.startsWith(`src${path.sep}`);
        })
        .map((f) => f.abs);
}

function clang_format_if_available(quiet) {
    const probe = spawnSync('clang-format', ['--version'], { stdio: 'ignore' });
    if (probe.status !== 0)
        return;

    const cFiles = find_c_files_for_format();
    if (cFiles.length === 0)
        return;

    const res = spawnSync('clang-format', ['-i', ...cFiles], { stdio: 'ignore' });
    if (!quiet && res.status === 0)
        process.stdout.write('Formatted: src/**/*.{c,h}\n');
}

function run_once() {
    const files = [];
    walk_dir(ROOT_DIR, '', files);
    let changed = 0;
    const quiet = process.argv.includes('--quiet');
    const format = process.argv.includes('--format');

    for (const f of files) {
        let content;
        try {
            content = fs.readFileSync(f.abs, 'utf8');
        } catch {
            continue;
        }

        const nl = normalize_newlines(content);
        const stripped = strip_comments_for_file(f.abs, nl);
        if (stripped !== nl) {
            fs.writeFileSync(f.abs, stripped, 'utf8');
            if (!quiet)
                process.stdout.write(`Cleaned: ${f.rel}\n`);
            changed++;
        }
    }

    if (format)
        clang_format_if_available(quiet);

    if (!quiet)
        process.stdout.write(`Done. Files changed: ${changed}\n`);
}

const watch = process.argv.includes('--watch');

if (watch) {
    run_once();
    setInterval(run_once, 10 * 60 * 1000);
} else {
    run_once();
}