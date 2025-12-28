# Git Source Control Guide

This document explains the git setup for the CO2-Ampel Pro NG project.

## Initial Setup

### 1. Initialize Git Repository

```bash
cd /Users/christian/Desktop/CO2-Ampel-Pro-NG
git init
```

### 2. What to Commit

The `.gitignore` file is already configured to exclude:
- Build artifacts (`.pio/`, `*.bin`, `*.elf`)
- IDE settings (`.vscode/`, `.idea/`)
- OS files (`.DS_Store`)
- Temporary files (`*.tmp`, `*.bak`, `*.log`)
- Work documentation (`workfiles/*.md`, `workfiles/*.sh`)
- Claude Code cache (`.claude/`)

### 3. Files Ready for Version Control

#### Essential Project Files ✅
```
✅ platformio.ini              - Build configuration
✅ setup.sh                    - Setup script
✅ README.md                   - Project documentation
✅ CHANGELOG.md                - Version history
✅ GIT_GUIDE.md                - This file
✅ .gitignore                  - Git ignore rules
```

#### Custom Board Definition ✅
```
✅ boards/co2ampel.json        - Custom board definition
✅ variants/co2ampel/          - Custom variant files
   ✅ variant.h                - Pin definitions
   ✅ variant.cpp              - Hardware initialization
   ✅ linker_scripts/gcc/      - Linker scripts
```

#### Source Code ✅
```
✅ src/main.cpp                - Main firmware
✅ include/                    - Project headers
✅ lib/                        - Custom libraries (if any)
```

#### Tests ✅
```
✅ test/hardware_test.cpp      - Hardware verification test
✅ test/README.md              - Test documentation
```

#### Documentation ✅
```
✅ workfiles/PROJECT_CONTEXT.json  - Complete project context
✅ workfiles/README.md             - Workfiles index
```

#### Reference (Optional) ⚠️
```
⚠️ legacy/                     - Original firmware (reference)
```
**Note**: The `legacy/` folder contains the original firmware for reference. You may choose to:
- **Include it**: Useful for comparing with original code
- **Exclude it**: Can be obtained from original GitHub repo
- **Recommended**: Include in first commit, then exclude future changes

## Commit Strategy

### First Commit - Complete Working State

```bash
# Add all essential files
git add platformio.ini setup.sh README.md CHANGELOG.md GIT_GUIDE.md .gitignore
git add boards/ variants/ src/ include/ lib/ test/
git add workfiles/PROJECT_CONTEXT.json workfiles/README.md

# Optional: Add legacy code for reference
git add legacy/

# Create initial commit
git commit -m "Initial commit: CO2-Ampel Pro NG PlatformIO migration

- Custom board definition for CO2-Ampel Pro hardware
- Full firmware ported from Arduino IDE to PlatformIO
- 6 build environments (Pro, Basic, Plus, COVID, Debug, LoRa)
- Comprehensive hardware test suite
- Complete project documentation

Hardware verified working:
- Status LED, Buzzer, WS2812 RGB LEDs, Button, USB Serial
- Firmware compiles and uploads successfully
- Device enumerates correctly (VID:PID 04D8:EACF)

Build stats: RAM 17.9%, Flash 32.6%
Status: Fully operational"
```

### Subsequent Commits

```bash
# Feature additions
git commit -m "feat: Add new feature description"

# Bug fixes
git commit -m "fix: Fix issue description"

# Documentation updates
git commit -m "docs: Update documentation"

# Configuration changes
git commit -m "config: Update build configuration"

# Performance improvements
git commit -m "perf: Improve performance of X"

# Refactoring
git commit -m "refactor: Restructure code for better maintainability"
```

## Git Workflow

### Daily Workflow

```bash
# Check status
git status

# View changes
git diff

# Stage changes
git add <files>

# Commit changes
git commit -m "description"

# View history
git log --oneline
```

### Branching Strategy

```bash
# Create feature branch
git checkout -b feature/sensor-improvements

# Work on feature
# ... make changes ...
git commit -m "feat: Improve CO2 sensor calibration"

# Switch back to main
git checkout main

# Merge feature
git merge feature/sensor-improvements

# Delete feature branch
git branch -d feature/sensor-improvements
```

## Recommended Branches

- **main** - Stable, tested code
- **develop** - Integration branch for features
- **feature/*** - Feature development
- **bugfix/*** - Bug fixes
- **hotfix/*** - Critical fixes

## Remote Repository Setup

### GitHub (Recommended)

```bash
# Create repository on GitHub, then:
git remote add origin https://github.com/yourusername/CO2-Ampel-Pro-NG.git
git branch -M main
git push -u origin main
```

### GitLab

```bash
git remote add origin https://gitlab.com/yourusername/CO2-Ampel-Pro-NG.git
git branch -M main
git push -u origin main
```

## .gitattributes (Optional)

Create `.gitattributes` for consistent line endings:

```
# Auto detect text files
* text=auto

# Source files
*.cpp text eol=lf
*.h text eol=lf
*.c text eol=lf
*.ino text eol=lf

# Config files
*.json text eol=lf
*.ini text eol=lf
*.md text eol=lf

# Scripts
*.sh text eol=lf

# Binary files
*.bin binary
*.elf binary
*.hex binary
*.jpg binary
*.png binary
```

## Important Notes

### Do NOT Commit
- ❌ Build artifacts (`.pio/`, compiled binaries)
- ❌ IDE-specific settings (`.vscode/`, `.idea/`)
- ❌ Personal work notes (already in `workfiles/*.md`, excluded)
- ❌ Sensitive data (WiFi passwords, API keys)
- ❌ Large binary files (firmware binaries can be regenerated)

### DO Commit
- ✅ Source code (`src/`, `include/`, `lib/`)
- ✅ Configuration (`platformio.ini`, custom board/variant files)
- ✅ Documentation (`README.md`, `CHANGELOG.md`, `PROJECT_CONTEXT.json`)
- ✅ Tests (`test/`)
- ✅ Build scripts (`setup.sh`)
- ✅ Git configuration (`.gitignore`, `.gitattributes`)

## Project Context Preservation

The `workfiles/PROJECT_CONTEXT.json` file contains:
- Complete hardware specifications
- All fixes and solutions applied during migration
- Pin mappings and I2C addresses
- Build configuration details
- Testing status and known issues
- Future work roadmap

**This file is critical for project continuity and should always be committed.**

## Tagging Releases

```bash
# Tag the initial working version
git tag -a v1.0.0 -m "Initial release: Full PlatformIO migration"

# Push tags to remote
git push origin --tags

# List tags
git tag -l
```

## Maintenance

### Update Changelog
Always update `CHANGELOG.md` when making significant changes:
1. Add entry under "Unreleased" section
2. When releasing, move to new version section
3. Commit with changelog update

### Update Project Context
When making major changes or fixes, update `workfiles/PROJECT_CONTEXT.json`:
- Add new fixes to `critical_fixes_applied`
- Update `testing_status`
- Update `known_issues`
- Update `future_work`

## Ready to Commit?

Your project is now **clean and ready for source control**.

To start:
```bash
git init
git add .
git commit -m "Initial commit: CO2-Ampel Pro NG PlatformIO migration"
```

All work documentation has been organized in `workfiles/` and only the essential context file (`PROJECT_CONTEXT.json`) will be committed.
