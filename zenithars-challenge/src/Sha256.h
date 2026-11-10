#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

// SHA-256 and HMAC-SHA-256, self-contained (FIPS 180-4, RFC 2104). Used to seal the run file.
namespace Sha256
{
	using Digest = std::array<std::uint8_t, 32>;

	namespace detail
	{
		inline constexpr std::array<std::uint32_t, 64> k{
			0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
			0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
			0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
			0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
			0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
			0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
			0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
			0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
		};

		[[nodiscard]] constexpr std::uint32_t rotr(std::uint32_t a_value, int a_bits) noexcept
		{
			return (a_value >> a_bits) | (a_value << (32 - a_bits));
		}

		inline void compress(std::array<std::uint32_t, 8>& a_state, const std::uint8_t* a_block) noexcept
		{
			std::array<std::uint32_t, 64> w{};
			for (int i = 0; i < 16; ++i) {
				w[i] = (static_cast<std::uint32_t>(a_block[i * 4]) << 24) | (static_cast<std::uint32_t>(a_block[i * 4 + 1]) << 16) |
					   (static_cast<std::uint32_t>(a_block[i * 4 + 2]) << 8) | static_cast<std::uint32_t>(a_block[i * 4 + 3]);
			}
			for (int i = 16; i < 64; ++i) {
				const auto s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
				const auto s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
				w[i] = w[i - 16] + s0 + w[i - 7] + s1;
			}
			auto a = a_state[0], b = a_state[1], c = a_state[2], d = a_state[3];
			auto e = a_state[4], f = a_state[5], g = a_state[6], h = a_state[7];
			for (int i = 0; i < 64; ++i) {
				const auto s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
				const auto ch = (e & f) ^ (~e & g);
				const auto t1 = h + s1 + ch + k[i] + w[i];
				const auto s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
				const auto maj = (a & b) ^ (a & c) ^ (b & c);
				const auto t2 = s0 + maj;
				h = g;
				g = f;
				f = e;
				e = d + t1;
				d = c;
				c = b;
				b = a;
				a = t1 + t2;
			}
			a_state[0] += a;
			a_state[1] += b;
			a_state[2] += c;
			a_state[3] += d;
			a_state[4] += e;
			a_state[5] += f;
			a_state[6] += g;
			a_state[7] += h;
		}
	}

	[[nodiscard]] inline Digest Hash(std::string_view a_data)
	{
		std::array<std::uint32_t, 8> state{ 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 };

		std::string message{ a_data };
		const auto  bits = static_cast<std::uint64_t>(a_data.size()) * 8;
		message.push_back(static_cast<char>(0x80));
		while (message.size() % 64 != 56) {
			message.push_back('\0');
		}
		for (int i = 7; i >= 0; --i) {
			message.push_back(static_cast<char>((bits >> (i * 8)) & 0xFF));
		}
		for (std::size_t offset = 0; offset < message.size(); offset += 64) {
			detail::compress(state, reinterpret_cast<const std::uint8_t*>(message.data()) + offset);
		}

		Digest digest{};
		for (std::size_t i = 0; i < 8; ++i) {
			digest[i * 4] = static_cast<std::uint8_t>(state[i] >> 24);
			digest[i * 4 + 1] = static_cast<std::uint8_t>(state[i] >> 16);
			digest[i * 4 + 2] = static_cast<std::uint8_t>(state[i] >> 8);
			digest[i * 4 + 3] = static_cast<std::uint8_t>(state[i]);
		}
		return digest;
	}

	[[nodiscard]] inline Digest Hmac(std::string_view a_key, std::string_view a_data)
	{
		std::string key{ a_key };
		if (key.size() > 64) {
			const auto hashed = Hash(key);
			key.assign(reinterpret_cast<const char*>(hashed.data()), hashed.size());
		}
		key.resize(64, '\0');

		std::string inner(64, '\0');
		std::string outer(64, '\0');
		for (std::size_t i = 0; i < 64; ++i) {
			inner[i] = static_cast<char>(key[i] ^ 0x36);
			outer[i] = static_cast<char>(key[i] ^ 0x5c);
		}
		inner.append(a_data);
		const auto innerHash = Hash(inner);
		outer.append(reinterpret_cast<const char*>(innerHash.data()), innerHash.size());
		return Hash(outer);
	}

	[[nodiscard]] inline std::string Hex(const Digest& a_digest)
	{
		static constexpr char digits[] = "0123456789abcdef";
		std::string           text;
		text.reserve(a_digest.size() * 2);
		for (const auto byte : a_digest) {
			text.push_back(digits[byte >> 4]);
			text.push_back(digits[byte & 0x0F]);
		}
		return text;
	}
}
