# First publication to GitHub (PowerShell)

1. Install **Git for Windows** and optionally **GitHub CLI** (`gh`).
2. Open PowerShell in this repository folder.
3. Check whether GitHub CLI is already authenticated:

   ```powershell
   gh auth status
   ```

4. If not logged in, run `gh auth login` once and follow the secure login prompts. If you already use SSH or Git Credential Manager, you may instead use your established Git remote/auth workflow.
5. Create and publish a public repository:

   ```powershell
   git init
   git branch -M main
   git add .
   git commit -m "Initial public release v0.4.9"
   gh repo create native-calendar-todo --public --source . --remote origin --push
   ```

6. After creation, change `@github` in the `.wh.cpp` header to your repository URL while keeping upstream attribution, commit, and push.

**Alternative:** create an empty public repository on GitHub, then use `git remote add origin https://github.com/YOUR_USERNAME/native-calendar-todo.git` followed by `git push -u origin main`. This still requires valid GitHub credentials.

**Check before publishing:** build works on your Windows 11 system, no secrets or user-specific data, and license/copyright remain intact.
