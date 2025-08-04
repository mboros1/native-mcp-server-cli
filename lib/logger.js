import winston from 'winston';
import { join } from 'path';

// Create logger instance
export const logger = winston.createLogger({
  level: process.env.LOG_LEVEL || 'debug',
  format: winston.format.combine(
    winston.format.timestamp(),
    winston.format.errors({ stack: true }),
    winston.format.printf(({ timestamp, level, message, ...meta }) => {
      let msg = `${timestamp} [${level.toUpperCase()}] ${message}`;
      if (Object.keys(meta).length) {
        msg += ' ' + JSON.stringify(meta);
      }
      return msg;
    })
  ),
  transports: [
    // Write to file
    new winston.transports.File({ 
      filename: 'mcp-server-node.log',
      level: 'debug'
    }),
    // Also log errors to separate file
    new winston.transports.File({ 
      filename: 'mcp-server-error.log', 
      level: 'error' 
    })
  ]
});

// Add console transport in development
if (process.env.NODE_ENV !== 'production') {
  logger.add(new winston.transports.Console({
    format: winston.format.combine(
      winston.format.colorize(),
      winston.format.simple()
    )
  }));
}