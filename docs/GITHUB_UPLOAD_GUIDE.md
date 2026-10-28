# Uploading to GitHub

1. On github.com, create a new **empty** repository named `rv32im-pipeline-cpu`
   (no README, no .gitignore, no licence: the project already has them).
2. Install Git (Command Prompt):

   ```bat
   winget install --id Git.Git -e --source winget
   ```

   Close the window and open a new Command Prompt so `git` is on the PATH.

3. From the project folder:

   ```bat
   cd /d "C:\Users\jemkox\Desktop\rtl project"
   git config --global user.name "jemkox"
   git config --global user.email "<your GitHub noreply address>"
   git init
   git add .
   git commit -m "RV32IM 5-stage pipelined CPU with C verification tools"
   git branch -M main
   git remote add origin https://github.com/jemkox/rv32im-pipeline-cpu.git
   git push -u origin main
   ```

   Git asks you to sign in to GitHub the first time you push.

Commit emails are public. Use the noreply address shown at
github.com/settings/emails (it looks like `12345678+jemkox@users.noreply.github.com`)
rather than a private address.

`.gitignore` already excludes the `build/` folder, so compiled tools, hex files
and simulation dumps are not uploaded. Run `run_tests.ps1` once before
uploading and confirm it prints `ALL TESTS PASSED`.
