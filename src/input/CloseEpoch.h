#pragma once
#include <cstdint>

namespace closeepoch
{
	// While a radial closes, the pad holds the right stick centred, no slot pointed, and the game's key off the pointed item, and it
	// lets go only when the radial reports closed. A wheel button pressed again at once reopens the radial before it ever reported
	// closed: the hold then stayed on the NEW wheel (the stick could point nothing, and the Equipment cell drew black - IWP-024).
	// This remembers how many openings the radial had reported when a close began; a close is over, without the radial reporting
	// closed, when the wheel button went down again or the radial reported another opening since.
	class Epoch
	{
	public:
		// end of every read: the pad's closing flag and the radial's opening count
		void Note(bool a_closing, std::uint32_t a_opens)
		{
			if (!a_closing) {
				m_seen = false;
			} else if (!m_seen) {
				m_seen = true;
				m_opens = a_opens;
			}
		}

		bool Superseded(bool a_closing, std::uint32_t a_opens, bool a_pressedAgain) const
		{
			return a_closing && m_seen && (a_pressedAgain || a_opens != m_opens);
		}

		void Reset() { m_seen = false; }

	private:
		bool          m_seen = false;
		std::uint32_t m_opens = 0;
	};
}
