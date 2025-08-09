# GitHub Pages Documentation Setup

## Quick Setup

To enable automatic documentation publishing for this repository:

### 1. Enable GitHub Pages

1. Go to your repository on GitHub
2. Click on **Settings** (in the repository, not your profile)
3. Scroll down to **Pages** in the left sidebar
4. Under **Source**, select **GitHub Actions**
5. Click **Save**

### 2. Documentation will auto-deploy

Once enabled, the documentation will automatically:
- Build when you push to `trunk` branch
- Deploy to `https://[your-username].github.io/native-mcp-server-cli/`
- Update whenever you modify source code or documentation

## What Gets Published

The workflow publishes:
- **API Documentation** - All classes, methods, and functions
- **Class Diagrams** - Interactive relationship diagrams
- **Source Browser** - Clickable source code with cross-references
- **Search Index** - Full-text search of all documentation

## Documentation Structure

```
https://[your-username].github.io/native-mcp-server-cli/
├── index.html           # Main documentation page
├── classes.html         # Class list
├── files.html          # File list
├── namespaces.html     # Namespace list
└── pages.html          # Additional documentation pages
```

## Branch-Based Documentation

By default, documentation is only published from the `trunk` branch. This ensures:
- Documentation matches the main branch code
- Pull requests don't overwrite production docs
- Documentation stays stable

### Testing Documentation in PRs

Pull requests will:
1. Build documentation (to verify it compiles)
2. Upload as artifacts (downloadable for review)
3. Validate structure (ensure all files generated)
4. NOT publish to GitHub Pages (only trunk publishes)

## Local Documentation Testing

### Generate Locally
```bash
# Install doxygen
brew install doxygen graphviz  # macOS
sudo apt-get install doxygen graphviz  # Ubuntu

# Generate docs
doxygen Doxyfile

# View docs
open docs/doxygen/html/index.html  # macOS
xdg-open docs/doxygen/html/index.html  # Linux
```

### Check for Issues
```bash
# Generate and show warnings
doxygen Doxyfile 2>&1 | grep -i warning

# Count documented items
find docs/doxygen/html -name "*.html" | wc -l
```

## Workflow Features

The documentation workflow (`documentation.yml`) includes:

### Automatic Generation
- Triggers on code changes to `src/` directory
- Runs on push to trunk
- Runs on pull requests for validation

### Quality Checks
- Validates documentation structure
- Counts documented classes and files
- Ensures minimum documentation coverage

### Graceful Failure Handling
- Continues even if Pages isn't enabled
- Provides clear setup instructions
- Uploads artifacts regardless of Pages status

## Troubleshooting

### "Get Pages site failed" Error
**Solution**: Enable GitHub Pages (see step 1 above)

### Documentation Not Updating
**Check**:
1. Workflow is running (Actions tab)
2. You're pushing to `trunk` branch
3. GitHub Pages is set to "GitHub Actions" source

### Missing Documentation
**Check**:
1. Doxygen comments are properly formatted
2. Files are included in Doxyfile INPUT
3. No syntax errors in source files

### Pages URL Not Working
**Wait**: First deployment can take 5-10 minutes
**Check**: Repository is public or you have GitHub Pro/Team

## Documentation Standards

See [Doxygen Style Guide](./doxygen-style-guide.md) for:
- Comment formatting standards
- Documentation priorities
- Example documentation patterns
- Best practices

## CI/CD Integration

The documentation system integrates with CI to:
- Block PRs with documentation errors
- Ensure documentation stays current
- Provide preview artifacts for review
- Auto-publish on merge to trunk

## Benefits

1. **Always Current** - Docs update with every code change
2. **Versioned** - Documentation matches code version
3. **Searchable** - Full-text search across all docs
4. **Interactive** - Clickable diagrams and cross-references
5. **Free Hosting** - GitHub Pages provides free hosting
6. **No Maintenance** - Fully automated generation

## Future Enhancements

Potential improvements:
- [ ] Version selector for multiple releases
- [ ] PDF generation for offline viewing
- [ ] Coverage badges showing documentation percentage
- [ ] Integration with README badges
- [ ] Multi-language documentation
- [ ] API usage examples from tests