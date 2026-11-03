export interface RouteContext {
  req: Request;
  url: URL;
  params: Record<string, string>;
  query: URLSearchParams;
}

export type RouteHandler = (ctx: RouteContext) => Promise<Response> | Response;

interface CompiledRoute {
  method: string;
  pattern: string;
  regex: RegExp;
  paramNames: string[];
  handler: RouteHandler;
}

const CORS_HEADERS: Record<string, string> = {
  "Access-Control-Allow-Origin": "*",
  "Access-Control-Allow-Methods": "GET, POST, PATCH, PUT, DELETE, OPTIONS",
  "Access-Control-Allow-Headers": "Content-Type, Authorization",
};

export function jsonResponse(
  data: unknown,
  status = 200,
  extraHeaders: Record<string, string> = {}
): Response {
  return new Response(JSON.stringify(data, null, 2), {
    status,
    headers: {
      "Content-Type": "application/json; charset=utf-8",
      ...CORS_HEADERS,
      ...extraHeaders,
    },
  });
}

export function errorResponse(message: string, status = 400, details?: unknown): Response {
  return jsonResponse(
    {
      error: {
        message,
        status,
        ...(details !== undefined ? { details } : {}),
      },
    },
    status
  );
}

export async function readJsonBody<T = Record<string, unknown>>(req: Request): Promise<T> {
  try {
    return (await req.json()) as T;
  } catch {
    throw new Error("Invalid JSON request body");
  }
}

export class OyezRouter {
  private routes: CompiledRoute[] = [];

  on(method: string, pattern: string, handler: RouteHandler): this {
    const paramNames: string[] = [];
    const regexSource =
      "^" +
      pattern
        .replace(/\/+$/, "")
        .replace(/:([a-zA-Z0-9_]+)/g, (_, name: string) => {
          paramNames.push(name);
          return "([^/]+)";
        }) +
      "/?$";

    this.routes.push({
      method: method.toUpperCase(),
      pattern,
      regex: new RegExp(regexSource === "^/?$" ? "^/$" : regexSource),
      paramNames,
      handler,
    });
    return this;
  }

  get(pattern: string, handler: RouteHandler): this {
    return this.on("GET", pattern, handler);
  }

  post(pattern: string, handler: RouteHandler): this {
    return this.on("POST", pattern, handler);
  }

  patch(pattern: string, handler: RouteHandler): this {
    return this.on("PATCH", pattern, handler);
  }

  delete(pattern: string, handler: RouteHandler): this {
    return this.on("DELETE", pattern, handler);
  }

  async handle(req: Request): Promise<Response> {
    if (req.method === "OPTIONS") {
      return new Response(null, { status: 204, headers: CORS_HEADERS });
    }

    const url = new URL(req.url);
    const pathname = url.pathname === "" ? "/" : url.pathname;
    const method = req.method.toUpperCase();

    for (const route of this.routes) {
      if (route.method !== method) continue;
      const match = route.regex.exec(pathname);
      if (!match) continue;

      const params: Record<string, string> = {};
      route.paramNames.forEach((name, idx) => {
        params[name] = decodeURIComponent(match[idx + 1] ?? "");
      });

      try {
        return await route.handler({
          req,
          url,
          params,
          query: url.searchParams,
        });
      } catch (err) {
        const msg = err instanceof Error ? err.message : "Internal server error";
        const status = msg.includes("Invalid JSON") ? 400 : 500;
        return errorResponse(msg, status);
      }
    }

    return errorResponse(`Route not found: ${method} ${pathname}`, 404);
  }
}
