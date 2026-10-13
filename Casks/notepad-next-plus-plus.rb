cask "notepad-next-plus-plus" do
  arch arm: "arm64", intel: "x86_64"

  version "0.15.1"
  sha256 arm:   "7fa6a88547715dc59ec3efc2b29b9107938a15a4a64a317639405a37b2440499",
         intel: "32b9aa41c573fa23220a810cde5a4ba699b23bd0c1a1a9b3f06cb003293f8b88"

  url "https://github.com/KMerdan/notepad-next-plus-plus/releases/download/v#{version}/NotepadNextPlusPlus-#{version}-macos-#{arch}.dmg"
  name "Notepad Next ++"
  desc "Text editor with native plugins and HTML previews"
  homepage "https://github.com/KMerdan/notepad-next-plus-plus"

  depends_on macos: :sonoma

  app "NotepadNextPlusPlus.app"
end
