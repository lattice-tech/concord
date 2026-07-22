#include <bgfx_compute.sh>

BUFFER_RW(s_particles, vec4, 0);

uniform vec4 u_gpuParticleSimulation[12];
uniform vec4 u_gpuParticleForceFields[16];
uniform mat4 u_gpuParticleWorld;
uniform mat4 u_gpuParticleRotation;
uniform mat4 u_gpuParticleInverseRotation;

#define PARTICLE_GROUP_SIZE 64
#define MAX_FORCE_FIELDS 8

uint Hash(uint value)
{
	value ^= value >> 16u;
	value *= 0x7feb352du;
	value ^= value >> 15u;
	value *= 0x846ca68bu;
	value ^= value >> 16u;
	return value;
}

float Random01(uint value)
{
	return float(Hash(value) & 0x00ffffffu) / 16777216.0;
}

vec3 RandomUnit(uint seed)
{
	float z = Random01(seed + 1u) * 2.0 - 1.0;
	float angle = Random01(seed + 2u) * 6.28318530718;
	float radius = sqrt(max(0.0, 1.0 - z * z));
	return vec3(cos(angle) * radius, z, sin(angle) * radius);
}

vec3 SafeDirection(vec3 direction)
{
	float magnitude = length(direction);
	return magnitude > 1e-5 ? direction / magnitude : vec3(0.0, 1.0, 0.0);
}

vec3 SampleCone(vec3 direction, float halfAngle, uint seed)
{
	vec3 forward = SafeDirection(direction);
	vec3 helper = abs(forward.y) < 0.99
		? vec3(0.0, 1.0, 0.0)
		: vec3(1.0, 0.0, 0.0);
	vec3 tangent = normalize(cross(helper, forward));
	vec3 bitangent = cross(forward, tangent);
	float cosine = mix(cos(halfAngle), 1.0, Random01(seed + 3u));
	float sine = sqrt(max(0.0, 1.0 - cosine * cosine));
	float azimuth = Random01(seed + 4u) * 6.28318530718;
	return SafeDirection(forward * cosine
		+ tangent * (cos(azimuth) * sine)
		+ bitangent * (sin(azimuth) * sine));
}

vec3 TransformDirection(mat4 transform, vec3 direction)
{
	float magnitude = length(direction);
	vec3 transformed = mul(transform, vec4(direction, 0.0)).xyz;
	return length(transformed) > 1e-5
		? normalize(transformed) * magnitude
		: vec3_splat(0.0);
}

vec3 SpawnPosition(uint shape, vec3 shapeSize, vec3 direction,
	float shapeAngle, uint seed)
{
	if (shape == 1u)
	{
		return RandomUnit(seed + 11u)
			* (pow(Random01(seed + 12u), 1.0 / 3.0) * shapeSize.x);
	}
	if (shape == 2u)
	{
		return (vec3(Random01(seed + 13u), Random01(seed + 14u),
			Random01(seed + 15u)) * 2.0 - 1.0) * shapeSize;
	}
	if (shape == 3u)
	{
		float radius = sqrt(Random01(seed + 16u)) * shapeSize.x;
		float angle = Random01(seed + 17u) * 6.28318530718;
		return vec3(cos(angle) * radius, 0.0, sin(angle) * radius);
	}
	if (shape == 4u)
	{
		return SampleCone(direction, shapeAngle, seed + 18u) * shapeSize.x;
	}
	return vec3_splat(0.0);
}

vec3 ForceAcceleration(vec3 position, int fieldIndex)
{
	vec4 positionType = u_gpuParticleForceFields[fieldIndex * 2];
	vec4 strengthRadius = u_gpuParticleForceFields[fieldIndex * 2 + 1];
	vec3 delta = positionType.xyz - position;
	float distanceToField = length(delta);
	float radius = strengthRadius.y;
	if (distanceToField < 1e-5 || (radius > 0.0 && distanceToField >= radius))
	{
		return vec3_splat(0.0);
	}
	float falloff = radius > 0.0 ? 1.0 - distanceToField / radius : 1.0;
	float acceleration = strengthRadius.x * falloff;
	if (floatBitsToUint(positionType.w) == 0u)
	{
		return delta / distanceToField * acceleration;
	}
	vec3 tangent = vec3(-delta.z, 0.0, delta.x);
	float tangentLength = length(tangent);
	return tangentLength > 1e-5
		? tangent / tangentLength * acceleration
		: vec3_splat(0.0);
}

NUM_THREADS(PARTICLE_GROUP_SIZE, 1, 1)
void main()
{
	uint particleId = gl_GlobalInvocationID.x;
	uint capacity = floatBitsToUint(u_gpuParticleSimulation[0].y);
	if (particleId >= capacity)
	{
		return;
	}

	uint base = particleId * 4u;
	vec4 positionAge = s_particles[base];
	vec4 velocityLifetime = s_particles[base + 1u];
	vec4 rotationSeed = s_particles[base + 2u];
	vec4 angularAlive = s_particles[base + 3u];
	float deltaTime = max(u_gpuParticleSimulation[0].x, 0.0);
	bool reset = floatBitsToUint(u_gpuParticleSimulation[1].w) != 0u;
	bool alive = !reset && angularAlive.w > 0.5;

	if (alive)
	{
		positionAge.w += deltaTime;
		if (positionAge.w >= velocityLifetime.w)
		{
			alive = false;
		}
	}

	if (alive && deltaTime > 0.0)
	{
		bool localSpace = floatBitsToUint(u_gpuParticleSimulation[1].z) != 0u;
		vec3 gravity = u_gpuParticleSimulation[4].xyz;
		if (localSpace)
		{
			gravity = TransformDirection(u_gpuParticleInverseRotation, gravity);
		}
		float dragFactor = max(0.0, 1.0 - u_gpuParticleSimulation[6].y * deltaTime);
		velocityLifetime.xyz = velocityLifetime.xyz * dragFactor + gravity * deltaTime;

		if (!localSpace)
		{
			int forceCount = int(floatBitsToUint(u_gpuParticleSimulation[9].y));
			for (int field = 0; field < MAX_FORCE_FIELDS; ++field)
			{
				if (field >= forceCount) { break; }
				velocityLifetime.xyz += ForceAcceleration(positionAge.xyz, field) * deltaTime;
			}
		}

		float turbulence = u_gpuParticleSimulation[8].w;
		float frequency = u_gpuParticleSimulation[9].x;
		if (turbulence > 0.0 && frequency > 0.0)
		{
			uint particleSeed = floatBitsToUint(rotationSeed.w);
			float phase = float(particleSeed & 0xffffu) * 0.0137
				+ u_gpuParticleSimulation[2].z * frequency;
			vec3 curl = vec3(
				cos(positionAge.y * frequency + phase) - cos(positionAge.z * frequency * 1.7 + phase),
				cos(positionAge.z * frequency * 1.1 + phase) - cos(positionAge.x * frequency + phase),
				cos(positionAge.x * frequency * 0.7 + phase) - cos(positionAge.y * frequency + phase));
			velocityLifetime.xyz += curl * (turbulence * deltaTime);
		}

		float maxSpeed = u_gpuParticleSimulation[6].z;
		float speed = length(velocityLifetime.xyz);
		if (maxSpeed > 0.0 && speed > maxSpeed)
		{
			velocityLifetime.xyz *= maxSpeed / speed;
		}
		positionAge.xyz += velocityLifetime.xyz * deltaTime;

		bool groundEnabled = floatBitsToUint(u_gpuParticleSimulation[7].w) != 0u;
		float groundY = u_gpuParticleSimulation[8].x;
		if (groundEnabled && positionAge.y < groundY && velocityLifetime.y < 0.0)
		{
			positionAge.y = groundY;
			velocityLifetime.y = -velocityLifetime.y * u_gpuParticleSimulation[8].y;
			velocityLifetime.xz *= u_gpuParticleSimulation[8].z;
		}
		rotationSeed.xyz += angularAlive.xyz * deltaTime;
	}

	uint spawnStart = floatBitsToUint(u_gpuParticleSimulation[0].z);
	uint spawnCount = floatBitsToUint(u_gpuParticleSimulation[0].w);
	uint relative = (particleId + capacity - spawnStart) % capacity;
	bool spawn = relative < spawnCount;
	if (spawn)
	{
		uint sequence = floatBitsToUint(u_gpuParticleSimulation[9].z) + relative;
		uint seed = Hash(sequence ^ floatBitsToUint(u_gpuParticleSimulation[1].x));
		vec3 direction = u_gpuParticleSimulation[3].xyz;
		vec3 localPosition = SpawnPosition(
			floatBitsToUint(u_gpuParticleSimulation[1].y),
			u_gpuParticleSimulation[5].xyz, direction,
			u_gpuParticleSimulation[5].w, seed);
		vec3 localVelocity = SampleCone(direction, u_gpuParticleSimulation[6].x,
			seed + 31u);
		float speed = mix(u_gpuParticleSimulation[3].w,
			u_gpuParticleSimulation[4].w, Random01(seed + 32u));
		localVelocity *= speed;

		bool localSpace = floatBitsToUint(u_gpuParticleSimulation[1].z) != 0u;
		if (localSpace)
		{
			positionAge.xyz = localPosition;
			vec3 inherited = TransformDirection(
				u_gpuParticleInverseRotation, u_gpuParticleSimulation[7].xyz);
			velocityLifetime.xyz = localVelocity
				+ inherited * u_gpuParticleSimulation[6].w;
		}
		else
		{
			positionAge.xyz = mul(u_gpuParticleWorld, vec4(localPosition, 1.0)).xyz;
			velocityLifetime.xyz = TransformDirection(u_gpuParticleRotation, localVelocity)
				+ u_gpuParticleSimulation[7].xyz * u_gpuParticleSimulation[6].w;
		}

		velocityLifetime.w = max(0.05, mix(u_gpuParticleSimulation[2].x,
			u_gpuParticleSimulation[2].y, Random01(seed + 33u)));
		positionAge.w = max(0.0, u_gpuParticleSimulation[2].w)
			* Random01(seed + 34u);
		rotationSeed.xyz = vec3_splat(0.0);
		rotationSeed.w = uintBitsToFloat(seed);
		angularAlive.xyz = mix(u_gpuParticleSimulation[10].xyz,
			u_gpuParticleSimulation[11].xyz,
			vec3(Random01(seed + 35u), Random01(seed + 36u), Random01(seed + 37u)));
		angularAlive.w = 1.0;

		alive = positionAge.w < velocityLifetime.w;
		if (alive && positionAge.w > 0.0)
		{
			vec3 gravity = u_gpuParticleSimulation[4].xyz;
			if (localSpace)
			{
				gravity = TransformDirection(u_gpuParticleInverseRotation, gravity);
			}
			positionAge.xyz += velocityLifetime.xyz * positionAge.w
				+ gravity * (0.5 * positionAge.w * positionAge.w);
			velocityLifetime.xyz += gravity * positionAge.w;
			rotationSeed.xyz += angularAlive.xyz * positionAge.w;
		}
	}

	angularAlive.w = alive ? 1.0 : 0.0;
	if (!alive)
	{
		positionAge.w = 0.0;
		velocityLifetime.w = 0.0;
	}
	s_particles[base] = positionAge;
	s_particles[base + 1u] = velocityLifetime;
	s_particles[base + 2u] = rotationSeed;
	s_particles[base + 3u] = angularAlive;
}
