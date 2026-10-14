cask "notepad-next-plus-plus" do
  arch arm: "arm64", intel: "x86_64"

  version "0.15.2"
  sha256 arm:   "e3f6022b40db5b481f5e6bdd13d0ba3578199ae2ddf99519690d7a9cf2873f19",
         intel: "e78372e3a58b13b4a16bdff5cc452462013d0bdb392f726bc25daff8ac0da75d"

  url "https://github.com/KMerdan/notepad-next-plus-plus/releases/download/v#{version}/NotepadNextPlusPlus-#{version}-macos-#{arch}.dmg"
  name "Notepad Next ++"
  desc "Text editor with native plugins and HTML previews"
  homepage "https://github.com/KMerdan/notepad-next-plus-plus"

  depends_on macos: :sonoma

  app "NotepadNextPlusPlus.app"
end
