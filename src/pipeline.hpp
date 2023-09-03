#pragma once

class Image;

class Pipeline final {
	public:
		Pipeline() = default;
		explicit Pipeline(const char* vertexSource, const char* fragmentSource);
		~Pipeline();

		Pipeline(Pipeline&&) noexcept;
		Pipeline& operator=(Pipeline&&) noexcept;

		Pipeline(const Pipeline&) = delete;
		void operator=(const Pipeline&) = delete;

		void set_uniform(unsigned uniform, int) const;

		void bind() const;
	private:
		unsigned m_vao{};
		unsigned m_program{};
};

