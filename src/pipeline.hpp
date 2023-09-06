#pragma once

class Image;

class Pipeline final {
	public:
		Pipeline() = default;
		explicit Pipeline(const char* vertexSource, const char* fragmentSource, unsigned primitive,
				unsigned vertexCount);
		~Pipeline();

		Pipeline(Pipeline&&) noexcept;
		Pipeline& operator=(Pipeline&&) noexcept;

		Pipeline(const Pipeline&) = delete;
		void operator=(const Pipeline&) = delete;

		void set_uniform(unsigned uniform, int) const;
		void set_uniform_float2(unsigned uniform, const float*) const;
		void set_uniform_float4(unsigned uniform, const float*) const;

		void bind() const;
		void draw() const;
	private:
		unsigned m_vao{};
		unsigned m_program{};
		unsigned m_primitive;
		unsigned m_vertexCount;
};

enum class PipelineIndex {
	RECT,
	MSDF,
	OUTLINE,
	COUNT,
	INVALID = COUNT,
};

inline Pipeline g_pipelines[static_cast<unsigned long long>(PipelineIndex::COUNT)]{};

void init_pipelines();

