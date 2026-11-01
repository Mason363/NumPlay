> **UNO is a [NumPlay](../../README.md) game that comes on its own.** It is too big to fit next to NumPlay on the calculator. Get `UNO.nwa` from the [latest release](https://github.com/Mason363/NumPlay/releases/latest).

<p align="center">
  <img src="src/icon.png" width="84" alt="UNO icon">
</p>

<h1 align="center">UNO</h1>

<p align="center">
  <b>The card game against one to three bots, on the NumWorks calculator.</b><br>
  The official rules, the 108-card deck and scoring to 500 points.
</p>

<p align="center">
  <a href="https://github.com/Mason363/NumPlay/releases/latest/download/UNO.nwa"><b>Download UNO.nwa</b></a>
  &nbsp;·&nbsp; <a href="#controls">Controls</a>
</p>

<p align="center">
  <img src="../../docs/media/gallery/uno.gif" width="640" alt="UNO: the bots play their cards in turn, then you pick a card in your hand and play it">
</p>

## Highlights

* **The official rules:** the 108-card deck, Skip, Reverse, Draw Two, Wild and Wild Draw Four, with the first card's effect, drawing a card you may play at once, and the two-player rules (Reverse and Skip let you play again).
* **The Wild Draw Four challenge:** it's only allowed with no card of the colour in play, but anyone can bluff. Challenge it and see the hand: a bluffer draws 4, otherwise you draw 6.
* **UNO!** Say it as you play your next-to-last card. Forget, and a bot may catch you for 2 cards. Catch a bot that forgot.
* **Scoring to 500:** the winner of each hand scores the cards left in the others' hands, and the deal moves to the left, until someone has 500 points. Or play a single hand.
* **Bots at three levels:** Easy plays any card that goes; Normal saves its Wilds and stops a player about to go out; Hard also counts the cards it has seen, plays to the colours you lack and may bluff.
* **House rules, if you want them:** stacking Draw Twos or Wild Draw Fours, drawing until you can play, having to play a drawn card, and UNO said for everyone. All off by default.
* **Saved as you go:** Home saves and quits, from anywhere. Your game is also copied into `uno_saves.py`, so installing the app again doesn't erase it.

<table>
  <tr>
    <td align="center"><img src="docs/shot.png" width="390" alt="Your turn at a table of four"><br><sub>Your turn: the cards that go are lit</sub></td>
    <td align="center"><img src="docs/colour.png" width="390" alt="Calling a colour"><br><sub>A Wild: call a colour</sub></td>
  </tr>
  <tr>
    <td align="center"><img src="docs/challenge.png" width="390" alt="A Wild Draw Four played on you"><br><sub>A Wild Draw Four: take it or challenge</sub></td>
    <td align="center"><img src="docs/reveal.png" width="390" alt="The challenged hand"><br><sub>The challenged hand, shown to you</sub></td>
  </tr>
  <tr>
    <td align="center"><img src="docs/uno.png" width="390" alt="Two cards left"><br><sub>Two cards left: time to say UNO</sub></td>
    <td align="center"><img src="docs/scores.png" width="390" alt="The scores after a hand"><br><sub>A hand won, on the way to 500</sub></td>
  </tr>
  <tr>
    <td align="center"><img src="docs/setup.png" width="390" alt="A new game"><br><sub>A new game: players, bots, length and house rules</sub></td>
    <td align="center"><img src="docs/title.png" width="390" alt="The title screen"><br><sub>The title screen</sub></td>
  </tr>
</table>

## How to play

Match the top card of the discard pile by colour, number or symbol, or play a Wild, which goes on anything. You can also draw a card instead; if it goes, you may play it at once. The coloured frame round the discard pile shows the colour to match, also after a Wild.

Skip makes the next player miss a turn, Reverse turns play round, and Draw Two makes the next player draw 2 and miss a turn. With a Wild you call the colour. A Wild Draw Four also makes the next player draw 4 and miss a turn, and it's only allowed when you have no card of the colour in play. The next player may challenge it: if it was a bluff, its player draws the 4; if not, the challenger draws 6.

Press **shift** as you play your next-to-last card, to say UNO. If you forget, anyone who catches you before the next player starts makes you draw 2; say it in time and you're safe. When a bot forgets, **shift** catches it. The first to play all their cards wins the hand and scores the cards left in the others' hands: numbers at face value, Skip, Reverse and Draw Two 20, Wilds 50. The first to 500 points wins the game.

## Controls

| Key | What it does |
| --- | --- |
| Left and Right | Pick a card in your hand |
| OK | Play the card |
| Up | Draw a card, keep the card you drew, or take the cards you have to |
| shift | Say UNO, or catch a bot that forgot |
| Back | Pause (Resume, New game, How to play, Quit game) |
| Home | Save and quit |

## Good to know

* **On its own:** UNO is too big to fit next to NumPlay. Installing it from the NumWorks website swaps out NumPlay and the other apps, and installing NumPlay again swaps it back out.
* **The classic deck:** the 108 cards of the classic game, without the blank cards and Wild Shuffle Hands of newer decks.
* **The table:** you and one to three bots. The original takes up to ten players.

## Credits

Inspired by [UNO](https://www.mattel.com/brands/uno), the card game by Mattel, not affiliated with Mattel. Go play the original with your friends, or the official app.

* The rules and scoring follow Mattel's instruction sheet. The cards are drawn by the game itself, after the classic deck's look, and the text uses NumPlay's **Nunito** font (SIL Open Font License).
* The calculator adaptation is by **Bobydeluxe**, who brought it to NumPlay.

Made by Mason Chen as part of NumPlay.

## License

The game's code is licensed under the GNU General Public License v3.0: see the `LICENSE` file at the root of the repository. UNO is a trademark of Mattel.
